// fl2000_surface.c
//
// (c)Copyright 2017, Fresco Logic, Incorporated.
//
// The contents of this file are property of Fresco Logic, Incorporated and are strictly protected
// by Non Disclosure Agreements. Distribution in any form to unauthorized parties is strictly prohibited.
//
// Purpose: Utility Primitives
//

#include "fl2000_include.h"
#include <linux/version.h>


/*
 * work-around get_user_pages API changes
 * for kernel version < 4.6.0
 * the function is declared as:
 * long get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
 *		    unsigned long start, unsigned long nr_pages,
 *		    int write, int force, struct page **pages,
 *		    struct vm_area_struct **vmas);
 *
 * for kernel version 4.6.0 ~ 4.8.17
 * the API changes to:
 * long get_user_pages(unsigned long start, unsigned long nr_pages,
 *			    int write, int force, struct page **pages,
 *			    struct vm_area_struct **vmas);
 *
 * for kernel version 4.9.0 ~ latest (as of 2017/08/09)
 * the API changes to:
 * long get_user_pages(unsigned long start, unsigned long nr_pages,
 *			    unsigned int gup_flags, struct page **pages,
 *			    struct vm_area_struct **vmas);
 */
long fl2000_get_user_pages(
	unsigned long start, unsigned long nr_pages,
 	struct page **pages, struct vm_area_struct **vmas)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0)
	return get_user_pages(
		current,
		current->mm,
		start,
		nr_pages,
		0,
		0,
		pages,
		vmas
		);
#elif LINUX_VERSION_CODE <= KERNEL_VERSION(4,8,17)
	return get_user_pages(
		start,
		nr_pages,
		0,
		0,
		pages,
		vmas
		);
#else
	return get_user_pages(
		start,
		nr_pages,
		FOLL_GET | FOLL_TOUCH,
		pages,
		vmas
		);
#endif
}

int fl2000_surface_pin_down(
	struct dev_ctx * dev_ctx,
	struct primary_surface* surface)
{
	int ret_val = 0;
	unsigned long start;
	unsigned long end;
	unsigned long nr_pages;
	struct page ** pages;
	int pages_pinned = 0;
	struct page *first_page;
	unsigned int i;
	unsigned long old_flags;
	struct vm_area_struct *vma;

	/*
	 * make sure start address is page-aligned.
	 */
	start = surface->user_buffer & PAGE_MASK;
	end = surface->user_buffer + surface->buffer_length;
	nr_pages = (end - start + PAGE_SIZE - 1) >> PAGE_SHIFT;

	pages = kmalloc(nr_pages * sizeof(struct page *), GFP_KERNEL);
	if (pages == NULL) {
		dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP, "no pages allocated?");
		ret_val = -ENOMEM;
		goto exit;
	}
	surface->pages = pages;
	surface->nr_pages = nr_pages;
	surface->pages_pinned = 0;

	/*
	 * for normal user surface, we lock it down by get_user_pages.
	 * for physical contiguous surface, the pages are persistent, and
	 * we don't need to lock them down. Just to track each pages into
	 * surface->pages.
	 */
	switch (surface->type) {
	case SURFACE_TYPE_VIRTUAL_FRAGMENTED_VOLATILE:
	case SURFACE_TYPE_VIRTUAL_FRAGMENTED_PERSISTENT:
		while (surface->pages_pinned != nr_pages) {
			down_read(&current->mm->mmap_sem);
			pages_pinned = fl2000_get_user_pages(
				surface->user_buffer,
				nr_pages,
				pages,
				NULL);
			up_read(&current->mm->mmap_sem);
			if (pages_pinned <= 0) {
				dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
					"get_user_pages fails with %d\n", pages_pinned);
				ret_val = pages_pinned;
				goto release_pages;
			}
			surface->pages_pinned += pages_pinned;
			dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
				"%d pages pinned\n", pages_pinned);
		}
		break;

	case SURFACE_TYPE_VIRTUAL_CONTIGUOUS:
		down_read(&current->mm->mmap_sem);
		/*
		 * work-around the user memory which is mapped from driver,
		 * but with VM_IO, VM_PFNMAP flags. This API assumes the mmaped user addr
		 * is backed by system memory.
		 */
		vma = find_vma(current->mm, surface->user_buffer);
		old_flags = vma->vm_flags;
		vma->vm_flags &= ~(VM_IO | VM_PFNMAP);
		pages_pinned = fl2000_get_user_pages(
			surface->user_buffer,
			nr_pages,
			pages,
			NULL);
		vma->vm_flags = old_flags;
		up_read(&current->mm->mmap_sem);
		if (pages_pinned <= 0) {
			dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
				"get_user_pages fails with %d\n", pages_pinned);
			ret_val = pages_pinned;
			goto release_pages;
		}

		dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
			"%d pages pinned\n", pages_pinned);

		surface->pages_pinned = pages_pinned;
		first_page = surface->pages[0];
		surface->physical_address = PFN_PHYS(page_to_pfn(first_page)) +
		surface->start_offset;
		break;

	case SURFACE_TYPE_PHYSICAL_CONTIGUOUS:
		surface->physical_address = surface->user_buffer;
//+++++  change pfn_to_page() to virt_to_page)....
//		first_page = pfn_to_page(__phys_to_pfn(surface->physical_address));
		first_page = virt_to_page(phys_to_virt(surface->physical_address));
		surface->first_page = first_page;
		for (i = 0; i < nr_pages; i++)
			pages[i] = first_page + i;
		dbg_msg(TRACE_LEVEL_INFO, DBG_PNP,
			"physical_address(0x%lx) derived from user_buffer(0x%lx)",
			(unsigned long) surface->physical_address,
			(unsigned long) surface->user_buffer);
		break;

	}

	goto exit;

release_pages:
	printk("---- page_cache_release(), page_cache_release() .....\n");
	for (i = 0; i < surface->pages_pinned; i++)
		//page_cache_release(pages[i]);
		//try_to_release_page(pages[i],0);
		put_page(pages[i]);
	kfree(pages);
	surface->pages = NULL;
	surface->nr_pages = 0;
	surface->pages_pinned = 0;

exit:
	return ret_val;
}

void fl2000_surface_unpin(
	struct dev_ctx * dev_ctx,
	struct primary_surface* surface)
{
	struct page ** pages = surface->pages;
	unsigned int pages_pinned = surface->pages_pinned;
	unsigned int i;

	if (surface->pages == NULL)
		return;

	printk("---!!!!!!! try_to_release_age()  for page_cache_release() .....\n");
	for (i = 0; i < pages_pinned; i++)
		//page_cache_release(pages[i]);
		//try_to_release_page(pages[i],0);
		  put_page(pages[i]);
	surface->pages = NULL;
	surface->nr_pages = 0;
	surface->pages_pinned = 0;
	kfree(pages);
}

int fl2000_surface_map(
	struct dev_ctx * dev_ctx,
	struct primary_surface* surface)
{
	int ret_val = 0;
	unsigned long page_offset = surface->user_buffer & ~PAGE_MASK;
	//+++++ print page_offset
	printk("------------ page_offset:0x%lx  -------------\n",page_offset);
	
	switch (surface->type) {
	case SURFACE_TYPE_VIRTUAL_FRAGMENTED_VOLATILE:
	case SURFACE_TYPE_VIRTUAL_FRAGMENTED_PERSISTENT:
	case SURFACE_TYPE_VIRTUAL_CONTIGUOUS:
	case SURFACE_TYPE_PHYSICAL_CONTIGUOUS:
		if (surface->pages == NULL || surface->nr_pages == 0) {
			dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
				"invalid pages(%p) or nr_pages(%d)",
				surface->pages, surface->nr_pages);
			ret_val = -EINVAL;
			goto exit;
		}
		surface->mapped_buffer = vm_map_ram(
			surface->pages,
			surface->nr_pages,
			-1,
			PAGE_KERNEL);
		if (surface->mapped_buffer == NULL) {
			dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP, "vm_map_ram failed?");
			ret_val = -ENOMEM;
			goto exit;
		}
		// +++++ print surface->mapped_buffer, after vm_map_ram()...
		printk("------------aft. vm_map_ram*(), surface->user_buffer = 0x%lx,surface->mapped_buffer = 0x%lx  -------------\n",surface->user_buffer,surface->mapped_buffer);

		break;

	default:
		break;
	}

	surface->system_buffer = surface->mapped_buffer + page_offset;

	dbg_msg(TRACE_LEVEL_INFO, DBG_PNP,
		"successfully mapped user addr(0x%lx) to system addr(0x%lx)",
		(unsigned long) surface->user_buffer,
		(unsigned long) surface->system_buffer);

exit:
	return ret_val;
}

void fl2000_surface_unmap(
	struct dev_ctx * dev_ctx,
	struct primary_surface* surface)
{
	if (surface->mapped_buffer == NULL)
		return;
	switch (surface->type) {
	case SURFACE_TYPE_VIRTUAL_FRAGMENTED_VOLATILE:
	case SURFACE_TYPE_VIRTUAL_FRAGMENTED_PERSISTENT:
	case SURFACE_TYPE_VIRTUAL_CONTIGUOUS:
	case SURFACE_TYPE_PHYSICAL_CONTIGUOUS:
		vm_unmap_ram(surface->mapped_buffer, surface->nr_pages);
		break;
	}
	surface->mapped_buffer = NULL;
	surface->system_buffer = NULL;
}

int fl2000_surface_create(
	struct dev_ctx * dev_ctx,
	struct surface_info * info)
{
	struct primary_surface* s;
	struct primary_surface* surface;
	int ret = 0;
	unsigned long flags;

	/*
	 * sanity check. The input color_format and pitch must match
	 */
	if (info->buffer_length != info->pitch * info->height) {
		dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
			"buffer_length(%u) should be %u?",
			(unsigned int ) info->buffer_length,
			info->pitch * info->height);
		ret = -EINVAL;
		goto exit;
	}
	switch (info->color_format) {
	case COLOR_FORMAT_RGB_24:
		if (info->pitch != info->width * 3) {
			dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
				"pitch(%d) should be %d?",
				info->pitch, info->width * 3);
			ret = -EINVAL;
			goto exit;
		}
		break;

	case COLOR_FORMAT_RGB_16_565:
	case COLOR_FORMAT_RGB_16_555:
		if (info->pitch != info->width * 2) {
			dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
				"pitch(%d) should be %d?",
				info->pitch, info->width * 3);
			ret = -EINVAL;
			goto exit;
		}
		break;

	default:
		dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
			"unknown color_format(%d)?",
			info->color_format);
		ret = -EINVAL;
		goto exit;
	}

	/*
	 * check if we have duplicated surface
	 */
	surface = NULL;
	spin_lock_bh(&dev_ctx->render.surface_list_lock);
	list_for_each_entry(s, &dev_ctx->render.surface_list, list_entry) {
		if (s->handle == info->handle) {
			surface = s;
			break;
		}
	}
	spin_unlock_bh(&dev_ctx->render.surface_list_lock);

	if (surface != NULL) {
		dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP, "duplicated surface(%x)?",
			(unsigned int) surface->handle);
		ret = -EINVAL;
		goto exit;
	}

	surface = kzalloc(sizeof(*surface), GFP_KERNEL);
	if (surface == NULL) {
		dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP, "no surface allocated?");
		ret = -ENOMEM;
		goto exit;
	}

	INIT_LIST_HEAD(&surface->list_entry);
	surface->handle		= info->handle;
	surface->user_buffer	= info->user_buffer;
	surface->buffer_length	= (uint32_t) info->buffer_length;
	surface->width		= info->width;
	surface->height		= info->height;
	surface->pitch		= info->pitch;
	surface->color_format	= info->color_format;
	surface->type		= info->type;
	surface->start_offset	= info->user_buffer & ~PAGE_MASK;

	/*
	 * always create shadow buffer no matter no matter the type is.
	 */
	surface->shadow_buffer = vmalloc(surface->buffer_length);
	if (surface->shadow_buffer == NULL) {
		dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP, "vmalloc failed?");
		kfree(surface);
		goto exit;
	}

	switch (info->type) {
	case SURFACE_TYPE_VIRTUAL_FRAGMENTED_PERSISTENT:
//+++++ delete if..else..: No matter RGB_24 or RGB_16, we MUST pin down and map here OR do it in fl2000_ioctl.c 
//		if (info->color_format == COLOR_FORMAT_RGB_24 &&
//		    info->pitch == info->width * 3) {
			ret = fl2000_surface_pin_down(dev_ctx, surface);
			if (ret < 0) {
				dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
					"fl2000_surface_pin_down(%x) failed?",
					(unsigned int) surface->handle);
				kfree(surface);
				goto exit;
			}

			ret = fl2000_surface_map(dev_ctx, surface);
			if (ret < 0) {
				dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
					"fl2000_surface_map(%x) failed?",
					(unsigned int) surface->handle);
				fl2000_surface_unpin(dev_ctx, surface);
				kfree(surface);
				goto exit;
			}

			/*
			 * if pixel_reordering is active, we always use shadow buffer
			 * where the shadow_buffer contains the re-ordered pixels
			 */
			surface->render_buffer = surface->shadow_buffer;
/*
		} else {
			surface->render_buffer = surface->shadow_buffer;
		}
*/
		break;

	case SURFACE_TYPE_VIRTUAL_FRAGMENTED_VOLATILE:
		surface->render_buffer = surface->shadow_buffer;
		break;

	case SURFACE_TYPE_VIRTUAL_CONTIGUOUS:
	case SURFACE_TYPE_PHYSICAL_CONTIGUOUS:
		ret = fl2000_surface_pin_down(dev_ctx, surface);
		if (ret < 0) {
			dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
				"fl2000_surface_pin_down(%x) failed?",
				(unsigned int) surface->handle);
			kfree(surface);
			goto exit;
		}

		ret = fl2000_surface_map(dev_ctx, surface);
		if (ret < 0) {
			dbg_msg(TRACE_LEVEL_ERROR, DBG_PNP,
				"fl2000_surface_map(%x) failed?",
				(unsigned int) surface->handle);
			fl2000_surface_unpin(dev_ctx, surface);
			kfree(surface);
			goto exit;
		}

		//+++++ change to surface->render_buffer = surface->system_buffer for VIRTUAL_CONTIGUOUS and PHYSICAL_GONTIGUOUS
		// !!!! but it will cause system corrupt!!!
		//surface->render_buffer = surface->shadow_buffer;
		surface->render_buffer = surface->system_buffer;
		break;

	default:
		break;
	}

	spin_lock_bh(&dev_ctx->render.surface_list_lock);
	list_add_tail(&surface->list_entry, &dev_ctx->render.surface_list);

	spin_lock_irqsave(&dev_ctx->count_lock, flags);
	dev_ctx->render.surface_list_count++;
	spin_unlock_irqrestore(&dev_ctx->count_lock, flags);

	spin_unlock_bh(&dev_ctx->render.surface_list_lock);

	dbg_msg(TRACE_LEVEL_INFO, DBG_PNP,
		"surface(%p) created for\n"
		"user_buffer(%x)/buffer_length(0x%x)\n"
		"width(%u)/height(%u)/pitch(%u)/type(%u),\n"
		"render_buffer(%p), system_buffer(%p), shadow_buffer(%p),\n"
		"surface_list_count(%u)",
		surface,
		(unsigned int) surface->user_buffer,
		(unsigned int) surface->buffer_length,
		surface->width,
		surface->height,
		surface->pitch,
		surface->type,
		surface->render_buffer,
		surface->system_buffer,
		surface->shadow_buffer,
		dev_ctx->render.surface_list_count);

exit:
	return ret;
}

void fl2000_surface_destroy(
	struct dev_ctx * dev_ctx,
	struct primary_surface* surface)
{
	dbg_msg(TRACE_LEVEL_INFO, DBG_PNP,
		"deleting surface(%p) user_buffer(0x%x)/buffer_length(%u)/"
		"width(%u)/height(%u)/pitch(%u)/type(%u),"
		"render_buffer(%p), surface_list_count(%u)\n",
		surface,
		(unsigned int) surface->user_buffer,
		(unsigned int) surface->buffer_length,
		surface->width,
		surface->height,
		surface->pitch,
		surface->type,
		surface->render_buffer,
		dev_ctx->render.surface_list_count);
        printk("fl2000_surface_unmap(dev_ctx, surface)...\n");
	fl2000_surface_unmap(dev_ctx, surface);
	printk("fl2000_surface_unpin(dev_ctx, surface)...------!!!!!!-----\n");
	fl2000_surface_unpin(dev_ctx, surface);
	if (surface->shadow_buffer) {
		printk("vfree(surface->shadow_buffer)...\n");
		vfree(surface->shadow_buffer);
		surface->shadow_buffer = NULL;
	}
	printk("kfree(surface)....\n");
	kfree(surface);
}

void fl2000_surface_destroy_all(struct dev_ctx * dev_ctx)
{
	struct primary_surface* surface;
	struct list_head * list_head = &dev_ctx->render.surface_list;
	unsigned long flags;

	spin_lock_bh(&dev_ctx->render.surface_list_lock);
	while (!list_empty(list_head)) {
		surface = list_first_entry(
			list_head, struct primary_surface, list_entry);
		list_del_init(&surface->list_entry);

		spin_lock_irqsave(&dev_ctx->count_lock, flags);
		dev_ctx->render.surface_list_count--;
		spin_unlock_irqrestore(&dev_ctx->count_lock, flags);

		spin_unlock_bh(&dev_ctx->render.surface_list_lock);

		dbg_msg(TRACE_LEVEL_INFO, DBG_PNP,
			"destroying surface(%p), surface_list_count(%u)\n",
			surface, dev_ctx->render.surface_list_count);

		fl2000_surface_destroy(dev_ctx, surface);

		spin_lock_bh(&dev_ctx->render.surface_list_lock);
	}
	spin_unlock_bh(&dev_ctx->render.surface_list_lock);
}

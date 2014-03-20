/*
 * inode.c
 *
 * Inode operations
 */

#include "struct.h"
#include <linux/mpage.h>
#include <linux/aio.h>
#include <uapi/linux/uio.h>

#pragma GCC optimize ("-O0")

static int lpfs_collect_inodes(struct lpfs *ctx, u64 ino, struct inode *inode);
int lpfs_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh, int create);

struct inode *lpfs_inode_lookup(struct lpfs *ctx, u64 ino)
{
        struct inode *inode = iget_locked(ctx->sb, ino);
        if (inode->i_state & I_NEW) {
                if (lpfs_collect_inodes(ctx, ino, inode)) {
                        iput(inode);
                        return NULL;
                } else {
                        return inode;
                }
        } else {
                return inode;
        }
}

static void lpfs_fill_inode(struct lpfs *ctx,
                struct inode *inode,
                struct lp_inode_fmt *d_inode);

int lpfs_collect_inodes(struct lpfs *ctx, u64 ino, struct inode *inode)
{                          
        struct buffer_head *bh;
	struct lpfs_inode_map *imap, *i_srch;
	struct lp_inode_fmt *head;
	struct inode *i_probe;
	u64 ino_blk;
	u64 ino_off;
	u64 head_off;

	i_srch = lpfs_imap_lookup(ctx, ino);
	if (!i_srch) {
		return -ENOENT;
	}

	bh = sb_bread(ctx->sb, i_srch->inode_byte_addr / LP_BLKSZ);
	if (bh == NULL) {
		return -EIO;
	}

	ihold(inode);

	for (head = (struct lp_inode_fmt *) bh->b_data;
	     LP_DIFF(bh->b_data, head) < LP_BLKSZ && head->ino > 0; ++head)
	{
		imap = head->ino == ino ? i_srch
					: lpfs_imap_lookup(ctx, head->ino);
		if (!imap) {
			continue;
		}

		if (head->ino == ino) {
			i_probe = inode;
		} else {
			i_probe = ilookup(ctx->sb, head->ino);
			if (i_probe) {
				goto skip;
			} else {
				i_probe = iget_locked(ctx->sb, head->ino);
			}
		}

		ino_blk = imap->inode_byte_addr / LP_BLKSZ;
		ino_off = imap->inode_byte_addr % LP_BLKSZ;
		head_off = LP_DIFF(bh->b_data, head);
		if (ino_blk == bh->b_blocknr && ino_off == head_off) {
			lpfs_fill_inode(ctx, i_probe, head);
		}
skip:
                iput(i_probe);
        }

        brelse(bh);
        return 0;
}


void lpfs_fill_inode(struct lpfs *ctx, struct inode *inode,
                struct lp_inode_fmt *d_inode)
{
        inode->i_ino = d_inode->ino;
        set_nlink(inode, d_inode->link_count);
        i_uid_write(inode, d_inode->uid);
        i_gid_write(inode, d_inode->gid);
        inode->i_version = d_inode->version;
        inode->i_size = d_inode->size;
        inode->i_atime = CURRENT_TIME;
        inode->i_mtime = ns_to_timespec(d_inode->mtime_usec * NSEC_PER_USEC);
        inode->i_ctime = ns_to_timespec(d_inode->ctime_usec * NSEC_PER_USEC);
        inode->i_blkbits = LP_BLKSZ_BITS;
        inode->i_blocks = (inode->i_size / LP_BLKSZ)
                + (blkcnt_t) ((inode->i_size % LP_BLKSZ) != 0);
        inode->i_mode = d_inode->mode;

        inode->i_sb = ctx->sb;

        inode->i_op = &lpfs_inode_ops;
        inode->i_mapping->a_ops = &lpfs_aops;

        if (inode->i_mode & S_IFDIR) {
                inode->i_fop = &lpfs_dir_ops;
        } else {
                inode->i_fop = &lpfs_file_ops;
        }

        unlock_new_inode(inode);
        insert_inode_hash(inode);
}


static unsigned lpfs_last_byte(struct inode *inode, unsigned long page_nr) 
{
        unsigned last_byte = inode->i_size;
        last_byte -= page_nr << PAGE_CACHE_SHIFT;
        if (last_byte > PAGE_CACHE_SIZE) {
                last_byte = PAGE_CACHE_SIZE;
        }
        return last_byte;
        // Find min(inode->i_size - page_nr << PAGE_CACHE_SHIFT, PAGE_CACHE_SIZE)
}

static int lpfs_readdir(struct file *file, struct dir_context *ctx)
{
        loff_t pos = ctx->pos;
        struct inode *inode = file->f_inode;
        struct super_block *sb = inode->i_sb;
        unsigned int offset = pos & ~PAGE_CACHE_MASK;
        unsigned long n = pos >> PAGE_CACHE_SHIFT;
        unsigned long npages = (inode->i_size+PAGE_CACHE_SIZE-1) >> PAGE_CACHE_SHIFT;

        if (pos > inode->i_size - (sizeof(struct linux_dirent64)+0)) {
                return 0;
        }

        for ( ; n < npages; n++, offset = 0) {
                char *kaddr, *limit;
                struct linux_dirent64 *de;
                int counter = 0;
                struct page *page = read_mapping_page(inode->i_mapping, n, NULL);
                // TODO: Do check later?
                kaddr = page_address(page);
                de = (struct linux_dirent64 *)(kaddr + offset);
                limit = kaddr + lpfs_last_byte(inode, n) - (sizeof(struct linux_dirent64)+0);
               
                while ((char *) de <= limit) {
                        if ((int)de->d_ino == 2) {
                                printk("Found crap! %d\n", counter);
          break;
                        }
                        printk("d_ino: %d\n", (int)de->d_ino);
                        de = (struct linux_dirent64 *)((char*)de + 1);
                        counter++;
                }
 
                // TODO: CHECK de->d_off and de->d_reclen
                for ( ; (char *)de <= limit; de = (struct linux_dirent64 *)((char *)de + de->d_off)) {
                        printk("de->d_off %lu de->d_reclen %d\n", (long unsigned int)de->d_off, (int)de->d_reclen);
                        if (de->d_reclen == 0) {
                                return -1;
                        }
                        if (de->d_ino) {
                                unsigned char t;
                                if (inode->i_mode & S_IFDIR) {
                                        t = DT_DIR;
                                } else if (inode->i_mode & S_IFREG) {
                                        t = DT_REG;
                                } else if (inode->i_mode & S_IFBLK ) {
                                        t = DT_BLK;
                                } else {
                                        t = DT_UNKNOWN;
                                }

                                if (!dir_emit(ctx, de->d_name, strlen(de->d_name), de->d_ino, t)) {
                                        kunmap(page);
                                        page_cache_release(page);
                                        return 0;
                                }
                        }
                        ctx->pos += de->d_off;  // or de->d_reclen
                }
                kunmap(page);
                page_cache_release(page);
        }
        
        (void) file; (void) ctx; (void) sb;
        // Template. To be replaced with bs version and later true vrs.
        return 0;
}



/* lpfs aops */
int lpfs_readpage(struct file *file, struct page *page) { 
	return mpage_readpage(page, lpfs_get_block);
}

int lpfs_writepage(struct page *page, struct writeback_control *wbc) {
	return block_write_full_page(page, lpfs_get_block, wbc);
}

int lpfs_readpages(struct file *filp, struct address_space *mapping,
                   struct list_head *pages, unsigned nr_pages) {
	return mpage_readpages(mapping, pages, nr_pages, lpfs_get_block);

}

int lpfs_writepages(struct address_space *mapping,
                    struct writeback_control *wbc) {
	return mpage_writepages(mapping, wbc, lpfs_get_block);
}

int lpfs_get_block(struct inode *inode, sector_t iblock,
                   struct buffer_head *bh_result, int create) {
	// if !create = read
	// iblock*512 = byte offset on disk
	// disk_size/block_size = number of blocks on disk
	// iblock*512/block_size = # of block on disk, remainder is offset within block
	
	/* Both ext2 and nilfs2 do this calculation */
	unsigned maxblocks = bh_result->b_size >> inode->i_blkbits;
        struct lpfs *l = (struct lpfs *)inode->i_sb->s_fs_info; 
	__u64 blknum = iblock;  //iblock*512/(int)l->sb_info.block_size; // after we get the blocknum somehow..
	map_bh(bh_result, inode->i_sb, blknum); 
	bh_result->b_size = (1 << inode->i_blkbits); //the first param is the number of blocks (ret in nilfs, # of contig blocks to read)
        (void) maxblocks;
	return 0;

	
}

static int lpfs_write_begin(struct file *file, struct address_space *mapping,
                             loff_t pos, unsigned len, unsigned flags,
                             struct page **pagep, void **fsdata)
{
        (void) file;
        return block_write_begin(mapping, pos, len, flags, pagep, lpfs_get_block);
}

static sector_t lpfs_bmap(struct address_space *mapping, sector_t block)
{
        return generic_block_bmap(mapping, block, lpfs_get_block);
}

static ssize_t lpfs_direct_IO(int rw, struct kiocb *iocb, 
                              const struct iovec *iov, loff_t offset,
                              unsigned long nr_segs)
{
        struct file *file = iocb->ki_filp;
        struct inode *inode = file->f_mapping->host;
        return blockdev_direct_IO(rw, iocb, inode, iov, offset, nr_segs, lpfs_get_block);
}

struct dentry *lpfs_lookup(struct inode *inode, struct dentry *dentry, unsigned int something) {
        (void) inode; (void) dentry; (void) something;
        return NULL;
}



void lpfs_destroy_inode(struct inode *inode)
{
	/* XXX: Mysterious VFS behavior allows stale I_FREEING inodes to
	 * hang around. */
	inode->i_state = 0;
}

struct inode_operations lpfs_inode_ops = {
        .setattr 	= simple_setattr,
        .getattr 	= simple_getattr, // stat(2) uses this
        // need atomic_open for dquot_file_open
        //
        //.atomic_open    = dquot_file_open,
        .lookup         = lpfs_lookup,
};

/* file.c */
struct file_operations lpfs_file_ops = {
        .llseek		= generic_file_llseek,
        .read		= do_sync_read,
        .write		= do_sync_write, // checkpoint 3

        .aio_read	= generic_file_aio_read,//do_sync_* calls these..
        .aio_write	= generic_file_aio_write,
        .mmap		= generic_file_mmap,

	.open		= generic_file_open,
	/* Palmer suggests the generic fsync */
	.fsync		= noop_fsync,



};
/* dir.c */
struct file_operations lpfs_dir_ops = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.fsync		= noop_fsync,
	.iterate 	= lpfs_readdir // need to implement


};

/* inode->i_mapping->aops assigned to a "alloc inode"
 * sort of function like ext2_iget or ramfs_get_inode
 *
 * ext2_readpage(struct file, struct page) -> mpage_readpage(page, ext2_get_block)
 * -> ext2_get_block(struct inode, sector_t iblock, struct buffer_head *bh_result,int create)
 *  which just sets bh_result->bsize...
 *
 * these were stolen from ext2
 */

struct address_space_operations lpfs_aops = {
        .readpage		= lpfs_readpage,
        .readpages		= lpfs_readpages,
        .writepage		= lpfs_writepage,
        .writepages		= lpfs_writepages,
        .write_begin		= lpfs_write_begin, // see piazza note
        .write_end		= generic_write_end, // see piazza note
        .bmap			= lpfs_bmap, //see piazza note
        .direct_IO		= lpfs_direct_IO // see piazza note
};

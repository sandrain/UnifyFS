#ifndef __UNIFYFS_INODE_H
#define __UNIFYFS_INODE_H

#include <pthread.h>
#include "tree.h"
#include "extent_tree.h"
#include "unifyfs_meta.h"
#include "unifyfs_global.h"

/**
 * @brief file and directory inode structure. this holds:
 */
struct unifyfs_inode {
    RB_ENTRY(unifyfs_inode) inode_tree_entry; /** tree entry for global inode
                                                tree */

    int gfid;                     /** global file identifier */
    unifyfs_file_attr_t attr;     /** file attributes */
    pthread_rwlock_t rwlock;      /** rwlock for accessing this structure */
    struct extent_tree* extents;  /** extent tree for data segments */
};

/**
 * @brief returns inode with given @gfid.
 *
 * @param gfid global file identifier
 *
 * @return inode structure on success, NULL otherwise.
 */
struct unifyfs_inode* unifyfs_inode_get(int gfid);

/**
 * @brief read lock the inode for ro access.
 *
 * @param ino inode structure to get access
 *
 * @return 0 on success, errno otherwise
 */
static inline int unifyfs_inode_rdlock(struct unifyfs_inode* ino)
{
    return pthread_rwlock_rdlock(&ino->rwlock);
}

/**
 * @brief write lock the inode for w+r access.
 *
 * @param ino inode structure to get access
 *
 * @return 0 on success, errno otherwise
 */
static inline int unifyfs_inode_wrlock(struct unifyfs_inode* ino)
{
    return pthread_rwlock_wrlock(&ino->rwlock);
}

/**
 * @brief unlock the inode.
 *
 * @param ino inode structure to unlock
 */
static inline void unifyfs_inode_unlock(struct unifyfs_inode* ino)
{
    pthread_rwlock_unlock(&ino->rwlock);
}

/**
 * @brief create a new inode with given parameters. The newly created inode
 * will be inserted to the global inode tree (global_inode_tree).
 *
 * @param gfid global file identifier.
 * @param attr attributes of the new file.
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_create(int gfid, unifyfs_file_attr_t* attr);

/**
 * @brief update the attributes of file with @gfid. The attributes are
 * selectively updated with unifyfs_file_attr_update() function (see
 * common/unifyfs_meta.h).
 *
 * @param gfid global file identifier
 * @param attr new attributes
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_update_attr(int gfid, unifyfs_file_attr_t* attr);

/**
 * @brief create a new or update an existing inode.
 *
 * @param gfid global file identifier
 * @param create try to create a new inode if set
 * @param attr file attributes
 *
 * @return 0 on success, errno otherwise
 */
static inline
int unifyfs_inode_metaset(int gfid, int create, unifyfs_file_attr_t* attr)
{
    int ret = 0;

    if (create) {
        ret = unifyfs_inode_create(gfid, attr);
    } else {
        ret = unifyfs_inode_update_attr(gfid, attr);
    }

    return ret;
}

/**
 * @brief read attributes for file with @gfid.
 *
 * @param gfid global file identifier
 * @param attr [out] file attributes to be filled
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_metaget(int gfid, unifyfs_file_attr_t* attr);

/**
 * @brief unlink file with @gfid. this will remove the target file inode from
 * the global inode tree.
 *
 * @param gfid global file identifier
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_unlink(int gfid);

/**
 * @brief truncate size of file with @gfid to @size.
 *
 * @param gfid global file identifier
 * @param size new file size
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_truncate(int gfid, unsigned long size);

/**
 * @brief get extent tree of file.
 *
 * @param gfid global file identifier
 *
 * @return extent tree if successful, NULL otherwise
 */
struct extent_tree* unifyfs_inode_get_extent_tree(int gfid);

/**
 * @brief add extents to file
 *
 * @param gfid global file identifier
 * @param extents extent tree to be added
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_add_extent(int gfid, struct extent_tree* extents);

/**
 * @brief get the maximum file size from the local extent tree of given file
 *
 * @param gfid global file identifier
 * @param offset [out] file offset to be filled by this function
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_get_extent_size(int gfid, size_t* offset);

#endif /* __UNIFYFS_INODE_H */


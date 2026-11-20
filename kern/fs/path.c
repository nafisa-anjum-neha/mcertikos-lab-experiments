// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include <kern/lib/types.h>
#include <kern/lib/debug.h>
#include <kern/lib/spinlock.h>
#include <thread/PTCBIntro/export.h>
#include <thread/PCurID/export.h>
#include "inode.h"
#include "dir.h"
#include "log.h"

// Paths

/**
 * Copy the next path element from path into name.
 * If the length of name is larger than or equal to DIRSIZ, then only
 * (DIRSIZ - 1) # characters should be copied into name.
 * This is because you need to save '\0' in the end.
 * You should still skip the entire string in this case.
 * Return a pointer to the element following the copied one.
 * The returned path has no leading slashes,
 * so the caller can check *path == '\0' to see if the name is the last one.
 * If no name to remove, return 0.
 *
 * Examples :
 *   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
 *   skipelem("///a//bb", name) = "bb", setting name = "a"
 *   skipelem("a", name) = "", setting name = "a"
 *   skipelem("", name) = skipelem("////", name) = 0
 */
static char *skipelem(char *path, char *name)
{
    char *p;
    int i, len;

    // Skip the leading slashes; no element left means no name to remove.
    while (*path == '/')
        path++;
    if (*path == '\0')
        return 0;

    // The element runs up to the next '/' or the end of the path.
    p = path;
    while (*p != '/' && *p != '\0')
        p++;

    // Copy it into name, leaving room for the terminator; a longer element
    // is truncated but still skipped entirely below.
    len = p - path;
    if (len > DIRSIZ - 1)
        len = DIRSIZ - 1;
    for (i = 0; i < len; i++)
        name[i] = path[i];
    name[len] = '\0';

    // Skip the slashes after the element and return the rest of the path
    // (possibly "", which is not 0, so the caller sees it as the last one).
    while (*p == '/')
        p++;
    return p;
}

/**
 * Look up and return the inode for a path name.
 * If nameiparent is true, return the inode for the parent and copy the final
 * path element into name, which must have room for DIRSIZ bytes.
 * Returns 0 in the case of error.
 */
static struct inode *namex(char *path, bool nameiparent, char *name)
{
    struct inode *ip, *next;

    // If path is a full path, get the pointer to the root inode. Otherwise get
    // the inode corresponding to the current working directory.
    if (*path == '/') {
        ip = inode_get(ROOTDEV, ROOTINO);
    } else {
        ip = inode_dup((struct inode *) tcb_get_cwd(get_curid()));
    }

    while ((path = skipelem(path, name)) != 0) {
        // 1. The current inode must be a locked directory to look into.
        inode_lock(ip);
        if (ip->type != T_DIR) {
            inode_unlockput(ip);
            return 0;
        }

        // 2. nameiparent stops one element early: this element (already in
        //    name) is the last one, so ip is the directory that contains it.
        if (nameiparent && *path == '\0') {
            inode_unlock(ip);
            return ip;
        }

        // 3. Look the element up in the current directory.
        next = dir_lookup(ip, name, 0);
        if (next == 0) {
            inode_unlockput(ip);
            return 0;
        }

        // 4. Continue with the inode found.
        inode_unlockput(ip);
        ip = next;
    }

    if (nameiparent) {
        inode_put(ip);
        return 0;
    }
    return ip;
}

/**
 * Return the inode corresponding to path.
 */
struct inode *namei(char *path)
{
    char name[DIRSIZ];
    return namex(path, FALSE, name);
}

/**
 * Return the inode corresponding to path's parent directory and copy the final
 * element into name.
 */
struct inode *nameiparent(char *path, char *name)
{
    return namex(path, TRUE, name);
}

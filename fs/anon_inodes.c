// SPDX-License-Identifier: GPL-2.0-only
/*
 *  fs/anon_inodes.c
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 *  Thanks to Arnd Bergmann for code review and suggestions.
 *  More changes for Thomas Gleixner suggestions.
 *
 */

#include <linux/cred.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/magic.h>
#include <linux/anon_inodes.h>
#include <linux/pseudo_fs.h>
#include <linux/security.h>

#include <linux/uaccess.h>

static struct vfsmount *anon_inode_mnt __read_mostly;
static struct inode *anon_inode_inode;

/*
 * anon_inodefs_dname() is called from d_path().
 */
static char *anon_inodefs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(dentry, buffer, buflen, "anon_inode:%s",
				dentry->d_name.name);
}

static const struct dentry_operations anon_inodefs_dentry_operations = {
	.d_dname	= anon_inodefs_dname,
};

static int anon_inodefs_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx = init_pseudo(fc, ANON_INODE_FS_MAGIC);
	if (!ctx)
		return -ENOMEM;
	ctx->dops = &anon_inodefs_dentry_operations;
	return 0;
}

static struct file_system_type anon_inode_fs_type = {
	.name		= "anon_inodefs",
	.init_fs_context = anon_inodefs_init_fs_context,
	.kill_sb	= kill_anon_super,
};

static struct inode *anon_inode_make_secure_inode(
	const char *name,
	const struct inode *context_inode)
{
	struct inode *inode;
	const struct qstr qname = QSTR_INIT(name, strlen(name));
	int error;

	inode = alloc_anon_inode(anon_inode_mnt->mnt_sb);
	if (IS_ERR(inode))
		return inode;
	inode->i_flags &= ~S_PRIVATE;
	error =	security_inode_init_security_anon(
		inode, &qname, context_inode);
	if (error) {
		iput(inode);
		return ERR_PTR(error);
	}
	return inode;
}

struct file *_anon_inode_getfile(const char *name,
				 const struct file_operations *fops,
				 void *priv, int flags,
				 const struct inode *context_inode,
				 bool secure)
{
	struct inode *inode;
	struct file *file;

	if (secure) {
		inode =	anon_inode_make_secure_inode(
			name, context_inode);
		if (IS_ERR(inode))
			return ERR_PTR(PTR_ERR(inode));
	} else {
		inode =	anon_inode_inode;
		if (IS_ERR(inode))
			return ERR_PTR(-ENODEV);
		/*
		 * We know the anon_inode inode count is always
		 * greater than zero, so ihold() is safe.
		 */
		ihold(inode);
	}

	if (fops->owner && !try_module_get(fops->owner)) {
		file = ERR_PTR(-ENOENT);
		goto err;
	}

	file = alloc_file_pseudo(inode, anon_inode_mnt, name,
				 flags & (O_ACCMODE | O_NONBLOCK), fops);
	if (IS_ERR(file))
		goto err;

	file->f_mapping = inode->i_mapping;

	file->private_data = priv;

	return file;

err:
	iput(inode);
	module_put(fops->owner);
	return file;
}

/**
 * anon_inode_getfile_secure - creates a new file instance by hooking
 *                             it up to a new anonymous inode and a
 *                             dentry that describe the "class" of the
 *                             file.  Make it possible to use security
 *                             modules to control access to the
 *                             new file.
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on an unspecified inode. This is
 * useful for files that do not need to have a full-fledged filesystem
 * to operate correctly.  All the files created with
 * anon_inode_getfile_secure() will have distinct inodes, avoiding
 * code duplication for the file/inode/dentry setup.  Returns the
 * newly created file* or an error pointer.
 */
struct file *anon_inode_getfile_secure(const char *name,
				       const struct file_operations *fops,
				       void *priv, int flags,
				       const struct inode *context_inode)
{
	return _anon_inode_getfile(
		name, fops, priv, flags, context_inode, true);
}
EXPORT_SYMBOL_GPL(anon_inode_getfile_secure);

/**
 * anon_inode_getfile - creates a new file instance by hooking it up to an
 *                      anonymous inode, and a dentry that describe the "class"
 *                      of the file
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on a single inode. This is useful for files
 * that do not need to have a full-fledged inode in order to operate correctly.
 * All the files created with anon_inode_getfile() will share a single inode,
 * hence saving memory and avoiding code duplication for the file/inode/dentry
 * setup.  Returns the newly created file* or an error pointer.
 */
struct file *anon_inode_getfile(const char *name,
				const struct file_operations *fops,
				void *priv, int flags)
{
	return _anon_inode_getfile(name, fops, priv, flags, NULL, false);
}
EXPORT_SYMBOL_GPL(anon_inode_getfile);

static int _anon_inode_getfd(const char *name,
			     const struct file_operations *fops,
			     void *priv, int flags,
			     const struct inode *context_inode,
			     bool secure)
{
	int error, fd;
	struct file *file;

	error = get_unused_fd_flags(flags);
	if (error < 0)
		return error;
	fd = error;

	file = _anon_inode_getfile(name, fops, priv, flags, context_inode,
				   secure);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto err_put_unused_fd;
	}
	fd_install(fd, file);

	return fd;

err_put_unused_fd:
	put_unused_fd(fd);
	return error;
}

/**
 * anon_inode_getfd_secure - creates a new file instance by hooking it
 *                           up to a new anonymous inode and a dentry
 *                           that describe the "class" of the file.
 *                           Make it possible to use security modules
 *                           to control access to the new file.
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on an unspecified inode. This is
 * useful for files that do not need to have a full-fledged filesystem
 * to operate correctly.  All the files created with
 * anon_inode_getfile_secure() will have distinct inodes, avoiding
 * code duplication for the file/inode/dentry setup.  Returns a newly
 * created file descriptor or an error code.
 */
int anon_inode_getfd_secure(const char *name, const struct file_operations *fops,
			    void *priv, int flags,
			    const struct inode *context_inode)
{
	return _anon_inode_getfd(name, fops, priv, flags,
				 context_inode, true);
}
EXPORT_SYMBOL_GPL(anon_inode_getfd_secure);

/**
 * anon_inode_getfd - creates a new file instance by hooking it up to
 *                    an anonymous inode and a dentry that describe
 *                    the "class" of the file
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on a single inode. This is
 * useful for files that do not need to have a full-fledged inode in
 * order to operate correctly.  All the files created with
 * anon_inode_getfile() will use the same singleton inode, reducing
 * memory use and avoiding code duplication for the file/inode/dentry
 * setup.  Returns a newly created file descriptor or an error code.
 */
int anon_inode_getfd(const char *name, const struct file_operations *fops,
		     void *priv, int flags)
{
	return _anon_inode_getfd(name, fops, priv, flags, NULL, false);
}
EXPORT_SYMBOL_GPL(anon_inode_getfd);

static int __init anon_inode_init(void)
{
	anon_inode_mnt = kern_mount(&anon_inode_fs_type);
	if (IS_ERR(anon_inode_mnt))
		panic("anon_inode_init() kernel mount failed (%ld)\n", PTR_ERR(anon_inode_mnt));

	anon_inode_inode = alloc_anon_inode(anon_inode_mnt->mnt_sb);
	if (IS_ERR(anon_inode_inode))
		panic("anon_inode_init() inode allocation failed (%ld)\n", PTR_ERR(anon_inode_inode));

	return 0;
}

fs_initcall(anon_inode_init);

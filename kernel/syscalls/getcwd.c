/*
 * fiwix/kernel/syscalls/getcwd.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fiwix/types.h>
#include <fiwix/fs.h>
#include <fiwix/errno.h>
#include <fiwix/string.h>
#include <fiwix/mm.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#include <fiwix/process.h>
#endif /*__DEBUG__ */

int sys_getcwd(char *buf, __size_t size)
{
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_getcwd(0x%08x, %d)\n", current->pid, (unsigned int)buf, size);
#endif /*__DEBUG__ */

	if((errno = check_user_area(VERIFY_WRITE, buf, size))) {
		return errno;
	}
	/* Psedocode:
	 * dir <- current->pwd
	 * if dir == /
	 *	return "/"
	 * cwd <- ""
	 * while dir != /
	 * 	p <- lookup(..)
	 *	open(p)
	 *	dirent <- read(p)
	 *	for d in dirent
	 *		if d == dir
	 *			cwd <- '/' + d.name + cwd
	 *			dir <- d
	 *			break
	 * return cwd
	 */
	char* dirent_buf;
	char* progress;
	if(!(dirent_buf = kmalloc())) {
		return -ENOMEM;
	}
	struct inode* i = current->pwd;
	struct inode* dir = i;
	struct inode* root;
	int tmp_fd;
	int space_dirents;
	__size_t marker = size-2;	/* Reserve '\0' at the end */
	__size_t namelength;
	char save;
	struct dirent* d_ptr;
	buf[size-1] = 0;
	if((errno = namei("/",&root,0,FOLLOW_LINKS))) {
		kfree(dirent_buf);
		return errno;
	}
	if (dir == root) {
		/* This case needs special handling, otherwise the loop skips over root */
		buf[0]='/';
		buf[1]='\0';
		kfree(dirent_buf);
		return 0;
	}
	while(dir != root) {
		if((errno = parse_namei("..",i,&dir,0,FOLLOW_LINKS))) {
			kfree(dirent_buf);
			return errno;
		}
		if((tmp_fd = get_new_fd(dir)) < 0) {
			kfree(dirent_buf);
			return tmp_fd;
		}
		space_dirents = dir->fsop->readdir(dir, &fd_table[tmp_fd],
				(struct dirent *)dirent_buf, PAGE_SIZE);
		release_fd(tmp_fd);
		progress = dirent_buf;
		while(progress < (dirent_buf + space_dirents)) {
			 d_ptr = (struct dirent *) progress;
			if(d_ptr->d_ino == i->inode) {
				namelength = strlen(d_ptr->d_name);
				marker -= namelength+1;	/* +1 for the leading '/' */
				save=buf[marker+namelength+1];
				strncpy(buf+marker+1, d_ptr->d_name, namelength);
				buf[marker] = '/';
				buf[marker+namelength+1] = save; /* strncp overrides '/' or '\0' */
				break;
			}
			progress += d_ptr->d_reclen;
		}
		/* Notify for a current bug */
		if (d_ptr->d_ino != i->inode) {
			printk("Bug: readdir of parent can't find the child.\n");
		}
		i = dir;
	}
	kfree(dirent_buf);
	/* Move the String to the start of the buffer */
	for (__size_t x = marker; x < size; x++) {
		buf[x-marker]=buf[x];
	}
	return 0;
}

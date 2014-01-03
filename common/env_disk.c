#include <common.h>
#include <command.h>
#include <environment.h>
#include <linux/stddef.h>
#include <malloc.h>
#include <ide.h>
#include <search.h>
#include <errno.h>

#if defined(CONFIG_CMD_SAVEENV) && defined(CONFIG_CMD_IDE)
#define CMD_SAVEENV
#elif defined(CONFIG_ENV_OFFSET_REDUND)
#error CONFIG_ENV_OFFSET_REDUND must have CONFIG_CMD_SAVEENV & CONFIG_CMD_IDE
#endif

#if defined(CONFIG_ENV_SIZE_REDUND) &&	\
	(CONFIG_ENV_SIZE_REDUND != CONFIG_ENV_SIZE)
#error CONFIG_ENV_SIZE_REDUND should be the same as CONFIG_ENV_SIZE
#endif

#ifndef CONFIG_ENV_RANGE
#define CONFIG_ENV_RANGE	CONFIG_ENV_SIZE
#endif

char *env_name_spec = "DISK";

#if defined(ENV_IS_EMBEDDED)
env_t *env_ptr = &environment;
#else /* ! ENV_IS_EMBEDDED */
env_t *env_ptr;
#endif /* ENV_IS_EMBEDDED */

DECLARE_GLOBAL_DATA_PTR;

int env_init(void)
{
#if defined(ENV_IS_EMBEDDED) 
	int crc1_ok = 0, crc2_ok = 0;
	env_t *tmp_env1;

#ifdef CONFIG_ENV_OFFSET_REDUND
	env_t *tmp_env2;

	tmp_env2 = (env_t *)((ulong)env_ptr + CONFIG_ENV_SIZE);
	crc2_ok = crc32(0, tmp_env2->data, ENV_SIZE) == tmp_env2->crc;
#endif
	tmp_env1 = env_ptr;
	crc1_ok = crc32(0, tmp_env1->data, ENV_SIZE) == tmp_env1->crc;

	if (!crc1_ok && !crc2_ok) {
		gd->env_addr	= 0;
		gd->env_valid	= 0;

		return 0;
	} else if (crc1_ok && !crc2_ok) {
		gd->env_valid = 1;
	}
#ifdef CONFIG_ENV_OFFSET_REDUND
	else if (!crc1_ok && crc2_ok) {
		gd->env_valid = 2;
	} else {
		/* both ok - check serial */
		if (tmp_env1->flags == 255 && tmp_env2->flags == 0)
			gd->env_valid = 2;
		else if (tmp_env2->flags == 255 && tmp_env1->flags == 0)
			gd->env_valid = 1;
		else if (tmp_env1->flags > tmp_env2->flags)
			gd->env_valid = 1;
		else if (tmp_env2->flags > tmp_env1->flags)
			gd->env_valid = 2;
		else /* flags are equal - almost impossible */
			gd->env_valid = 1;
	}

	if (gd->env_valid == 2)
		env_ptr = tmp_env2;
	else
#endif
	if (gd->env_valid == 1)
		env_ptr = tmp_env1;

	gd->env_addr = (ulong)env_ptr->data;

#else /* ENV_IS_EMBEDDED */
	gd->env_addr	= (ulong)&default_environment[0];
	gd->env_valid	= 1;
#endif /* ENV_IS_EMBEDDED */

	return 0;
}

#ifdef CMD_SAVEENV

int writeenv(size_t offset, u_char *buf)
{
	unsigned long written = ide_write(CONFIG_DISK_ENV_DEVICE, offset/512, CONFIG_ENV_SIZE/512, (ulong*)buf);

	if (written != CONFIG_ENV_SIZE/512)
		return 1;

	return 0;
}

#ifdef CONFIG_ENV_OFFSET_REDUND
static unsigned char env_flags;

int saveenv(void)
{
	env_t	env_new;
	ssize_t	len;
	char	*res;
	int	ret = 0;

	if (CONFIG_ENV_RANGE < CONFIG_ENV_SIZE)
		return 1;

	res = (char *)&env_new.data;
	len = hexport_r(&env_htab, '\0', 0, &res, ENV_SIZE, 0, NULL);
	if (len < 0) {
		error("Cannot export environment: errno = %d\n", errno);
		return 1;
	}
	env_new.crc	= crc32(0, env_new.data, ENV_SIZE);
	env_new.flags	= ++env_flags; /* increase the serial */

	if (gd->env_valid == 1) {
		puts("Writing to redundant DISK... ");
		ret = writeenv(CONFIG_ENV_OFFSET_REDUND, (u_char *)&env_new);
	} else {
		puts("Writing to DISK... ");
		ret = writeenv(CONFIG_ENV_OFFSET, (u_char *)&env_new);
	}
	if (ret) {
		puts("FAILED!\n");
		return 1;
	}

	puts("done\n");

	gd->env_valid = gd->env_valid == 2 ? 1 : 2;

	return ret;
}
#else /* ! CONFIG_ENV_OFFSET_REDUND */
int saveenv(void)
{
	int	ret = 0;
	ALLOC_CACHE_ALIGN_BUFFER(env_t, env_new, 1);
	ssize_t	len;
	char	*res;

	if (CONFIG_ENV_RANGE < CONFIG_ENV_SIZE)
		return 1;

	res = (char *)&env_new->data;
	len = hexport_r(&env_htab, '\0', 0, &res, ENV_SIZE, 0, NULL);
	if (len < 0) {
		error("Cannot export environment: errno = %d\n", errno);
		return 1;
	}
	env_new->crc = crc32(0, env_new->data, ENV_SIZE);

	puts("Writing to DISK... ");
	if (writeenv(CONFIG_ENV_OFFSET, (u_char *)env_new)) {
		puts("FAILED!\n");
		return 1;
	}

	puts("done\n");
	return ret;
}
#endif /* CONFIG_ENV_OFFSET_REDUND */
#endif /* CMD_SAVEENV */

int readenv(size_t offset, u_char *buf)
{
	unsigned long read = ide_read(CONFIG_DISK_ENV_DEVICE, offset/512, CONFIG_ENV_SIZE/512, (ulong*)buf);

	if (read != CONFIG_ENV_SIZE/512)
		return 1;

	return 0;
}

#ifdef CONFIG_ENV_OFFSET_REDUND
void env_relocate_spec(void)
{
#if !defined(ENV_IS_EMBEDDED)
	int read1_fail = 0, read2_fail = 0;
	int crc1_ok = 0, crc2_ok = 0;
	env_t *ep, *tmp_env1, *tmp_env2;

	tmp_env1 = (env_t *)malloc(CONFIG_ENV_SIZE);
	tmp_env2 = (env_t *)malloc(CONFIG_ENV_SIZE);
	if (tmp_env1 == NULL || tmp_env2 == NULL) {
		puts("Can't allocate buffers for environment\n");
		set_default_env("!malloc() failed");
		goto done;
	}

	read1_fail = readenv(CONFIG_ENV_OFFSET, (u_char *) tmp_env1);
	read2_fail = readenv(CONFIG_ENV_OFFSET_REDUND, (u_char *) tmp_env2);

	if (read1_fail && read2_fail)
		puts("*** Error - No Valid Environment Area found\n");
	else if (read1_fail || read2_fail)
		puts("*** Warning - some problems detected "
		     "reading environment; recovered successfully\n");

	crc1_ok = !read1_fail &&
		(crc32(0, tmp_env1->data, ENV_SIZE) == tmp_env1->crc);
	crc2_ok = !read2_fail &&
		(crc32(0, tmp_env2->data, ENV_SIZE) == tmp_env2->crc);

	if (!crc1_ok && !crc2_ok) {
		set_default_env("!bad CRC");
		goto done;
	} else if (crc1_ok && !crc2_ok) {
		gd->env_valid = 1;
	} else if (!crc1_ok && crc2_ok) {
		gd->env_valid = 2;
	} else {
		/* both ok - check serial */
		if (tmp_env1->flags == 255 && tmp_env2->flags == 0)
			gd->env_valid = 2;
		else if (tmp_env2->flags == 255 && tmp_env1->flags == 0)
			gd->env_valid = 1;
		else if (tmp_env1->flags > tmp_env2->flags)
			gd->env_valid = 1;
		else if (tmp_env2->flags > tmp_env1->flags)
			gd->env_valid = 2;
		else /* flags are equal - almost impossible */
			gd->env_valid = 1;
	}

	free(env_ptr);

	if (gd->env_valid == 1)
		ep = tmp_env1;
	else
		ep = tmp_env2;

	env_flags = ep->flags;
	env_import((char *)ep, 0);

done:
	free(tmp_env1);
	free(tmp_env2);

#endif /* ! ENV_IS_EMBEDDED */
}
#else /* ! CONFIG_ENV_OFFSET_REDUND */

void env_relocate_spec(void)
{
#if !defined(ENV_IS_EMBEDDED)
	int ret;
	ALLOC_CACHE_ALIGN_BUFFER(char, buf, CONFIG_ENV_SIZE);

	ret = readenv(CONFIG_ENV_OFFSET, (u_char *)buf);
	if (ret) {
		set_default_env("!readenv() failed");
		return;
	}

	env_import(buf, 1);
#endif /* ! ENV_IS_EMBEDDED */
}
#endif /* CONFIG_ENV_OFFSET_REDUND */

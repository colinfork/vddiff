#ifndef UZP_H
#define UZP_H

#include <sys/types.h>
#include "exec.h"

#define TMPPREFIX "/." BIN "."

enum uz_id { UZ_NONE, UZ_GZ, UZ_BZ2, UZ_TAR, UZ_TGZ, UZ_TBZ, UZ_ZIP,
    UZ_XZ, UZ_TXZ, UZ_TAR_Z };

struct uz_ext {
	const char *str;
	enum uz_id id;
};

extern const char *tmpdirbase;
extern char *tmp_dir;
extern char *vpath[2];
extern size_t vpthsz[2];
extern size_t spthofs[2];
extern size_t vpthofs[2];

struct filediff *unpack(const struct filediff *, int, char **, int);
void rmtmpdirs(const char *const);
int uz_init(void);
void uz_add(char *, char *);
void uz_exit(void);
const char *gettmpdirbase(void);
/*
 * Called before output af path to UI
 * Input
 *   i: 0: syspth[0], 1: syspth[1], 2: both paths
 */
void setvpth(const int i);
/*
 * Called when archive is entered
 * Input
 *   mode
 *     0/1: side, 2: side 1 only
 *     4: Started from main()
 *   fn: archive file name
 *   tn: temp dir name
 */
void setpthofs(const int mode, const char *const, const char *const);
void respthofs(int);

#endif /* UZP_H */

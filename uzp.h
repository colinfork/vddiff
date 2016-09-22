enum uz_id { UZ_NONE, UZ_GZ, UZ_BZ2, UZ_TAR, UZ_TGZ, UZ_TBZ };

struct uz_ext {
	char *str;
	enum uz_id id;
};

struct filediff *unzip(struct filediff *, int);
void rmtmpdirs(void);
void uz_init(void);
void pop_path(void);

extern short noic, magic, nows, scale;
extern short regex;

int test_fkey(int, unsigned short);
void ui_srch(void);
int srch_file(char *);
void disp_regex(void);
void clr_regex(void);
void start_regex(char *);
int regex_srch(int);
void parsopt(char *);
void bindiff(void);
void anykey(void);
void free_zdir(struct filediff *, char *);
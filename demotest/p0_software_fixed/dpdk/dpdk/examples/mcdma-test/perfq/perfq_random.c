#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "perfq_app.h"
#include "perfq_random.h"

#ifdef IFC_MCDMA_RANDOMIZATION
static FILE *fp;

static int compose_cmdline(int *i, char **cmdl, char *str)
{
        int n = 0;

		*i = n;
        char *token  = strtok(str, "-");

		if (token == NULL) {
			printf("RANDOM: no token exist\n");
			return -1;
		}

		sprintf(cmdl[n], "%s", "mcdma");
		++n;

        while(token) {
                sprintf(cmdl[n], "-%s", token);
                ++n;

                token = strtok(NULL, "-");
        }
        *i = n;

	return 0;
}

static int open_conf(void)
{

	struct stat file_stat;
	int ret;

	ret = lstat("./rand.conf", &file_stat);
	if (ret == 0) {
		if (S_ISLNK(file_stat.st_mode)){
			printf("Can't open file as it is simlink\n");
			return -1;
		}
	}

	if((fp	= fopen("./rand.conf", "r+")) == NULL) {
		printf("failed to open rand.conf file\n");
		return -1;
	}
	
	return 0;
}

static int ifc_rand_get_tokval(char* token)
{
	char *subtoken;

	if((subtoken = strtok(token, " ")))
		subtoken = strtok(NULL, " ");

	return atoi(subtoken);
}


static int get_rand_enteries(void)
{
	char	buff[512];
	int enteries = 0;

	if(fp == NULL) {
		if(open_conf() == -1) {
			return -1;
		}
	}

	while(!feof(fp)) {
		if(fgets(buff, 512, fp) == NULL) {
			fclose(fp);
			fp = NULL;
			break;
		}
		if(strchr(buff, '-')) {
			enteries++;
		}
	}
	if(fp){
		fclose(fp);
		fp = NULL;
	}

	return enteries;
}


static void load_params(int argc, char *argv[], struct rand_params_st *rp_p, int index)
{
	int		opt;
	char	*token;

	if(argv == NULL || rp_p == NULL)
		return;
	
	
	for(opt = 1; opt < argc; opt++)
	{
		token = strtok(argv[opt], "-");
		switch(token[0])
		{
		case 'p':
			rp_p[index].hifun_p = ifc_rand_get_tokval(token);
			break;
		}
	}
	
}

int load_rand_conf(struct rand_params_st **rp_p)
{
	char	buff[512];
	int		idx = 0;
	int		total_rand_enteries = 0;
	int		rand_argc;
	char	*rand_argv[64];
	char	rand_params[64][64];

	if ((total_rand_enteries = get_rand_enteries()) < 1) {
		return 0;
	}

	if(fp == NULL) {
		if(open_conf() == -1) {
			return -1;
		}
	}

//	*rp_p = (struct rand_params_st *) rte_zmalloc("rand_params_st", sizeof(struct rand_params_st)*total_rand_enteries, 0);
	*rp_p = (struct rand_params_st *) malloc(sizeof(struct rand_params_st)*total_rand_enteries);
	if (*rp_p == NULL) {
		fclose(fp);
		fp = NULL;
		return 0;
	}

	memset(*rp_p, 0, sizeof(struct rand_params_st)*total_rand_enteries);

	for(rand_argc = 0; rand_argc < 64; rand_argc++)
			rand_argv[rand_argc] = (char *)&rand_params[rand_argc];
	rand_argc = 0;

	
	while(!feof(fp)) {
		if(fgets(buff, 512, fp) == NULL) {
			fclose(fp);
			fp = NULL;
			break;
		}
		if(strchr(buff, '-')) {
			if(compose_cmdline(&rand_argc, (char**)&rand_argv, buff) == -1) {
				continue;
			}
			load_params(rand_argc, rand_argv, *rp_p, idx);
		}
		idx++;
	}

	
	if (fp) {	
		fclose(fp);
		fp = NULL;
	}

#ifdef RAND_DEBUG
	for(idx = 0; idx < total_rand_enteries; idx++) {
		printf("%d::%s::+++++++++++++ rand_argv[%d].hifun_p = %lu ++++++++++++++++++\n", __LINE__, __func__, idx, (*rp_p)[idx].hifun_p);
	}
#endif
	return total_rand_enteries;
}

int ifc_rand_get_next_entry(uint32_t option, struct queue_context *tctx)
{
	int	idx;
	
	if(option & IFC_RAND_HIFUN_P) {
		tctx->rand_f_idx++;
		
		if(tctx->rand_f_idx > tctx->flags->rand_enteries-1) {
			tctx->rand_f_idx = 0;
		}
		
		idx = (tctx->rand_f_idx==0)?tctx->flags->rand_enteries-1:tctx->rand_f_idx-1;
		
		while(tctx->rand_f_idx != idx){
			if(tctx->flags->rand_param_list[tctx->rand_f_idx].hifun_p > 0
				&& tctx->flags->rand_param_list[tctx->rand_f_idx].hifun_p <= tctx->flags->packet_size){
				return tctx->flags->rand_param_list[tctx->rand_f_idx].hifun_p;
			}
			
			tctx->rand_f_idx++;
			
			if(tctx->rand_f_idx > tctx->flags->rand_enteries-1) {
				tctx->rand_f_idx = 0;
			}
			
			if(tctx->rand_f_idx == idx) {
				if(tctx->flags->rand_param_list[tctx->rand_f_idx].hifun_p > 0
					&& tctx->flags->rand_param_list[tctx->rand_f_idx].hifun_p <= tctx->flags->packet_size){
					return tctx->flags->rand_param_list[tctx->rand_f_idx].hifun_p;
				}
				else{
					return 64;
				}
			}
		}
	}

	return 64;
}
#endif

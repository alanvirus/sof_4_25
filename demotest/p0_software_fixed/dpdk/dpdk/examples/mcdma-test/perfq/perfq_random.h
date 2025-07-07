#if !defined __PERFQ_RANDOM__
#define __PERFQ_RANDOM__
#ifdef IFC_MCDMA_RANDOMIZATION
int load_rand_conf(struct rand_params_st **rp_p);
int ifc_rand_get_next_entry(uint32_t option, struct queue_context *tctx);
#endif
#endif

#include <media/msm_vidc.h>
#include <linux/kref.h>

//copied from ion.c, remember to sync
struct ion_handle {
	struct kref ref;
	struct ion_client *client;
	struct ion_buffer *buffer;
	struct rb_node node;
	unsigned int kmap_cnt;
	int id;
};

void *htc_msm_smem_new_client(enum smem_type mtype,
                                struct msm_vidc_platform_resources *res);

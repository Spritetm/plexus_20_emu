#include "scsi.h"


//Create a new SCSI device. cow_dir can be NULL or "" to not use COW.
scsi_dev_t *scsi_dev_hd_new(const char *imagename, const char *cow_dir);


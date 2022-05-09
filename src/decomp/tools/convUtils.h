#pragma once
#include "../pc/libaudio_internal.h"

#define read_u16_le(p) ((uint8_t*)p)[1] * 0x100u + ((uint8_t*)p)[0]

struct EnvelopeMeta {
	int loaded;
	int size;
	uintptr_t orig;
	struct Envelope* addr;
};

struct seqFile* parse_seqfile(unsigned char* seq);
struct CTL* parse_ctl_data(unsigned char* ctlData, uintptr_t* pos);
struct TBL* parse_tbl_data(unsigned char* tbl);
struct SEQ* parse_seq_data(unsigned char* seq);
void update_CTL_sample_pointers(struct seqFile* ctl,struct seqFile* tbl);
#define INITIAL_GFX_ALLOC 10
#define INITIAL_GEO_ALLOC 10
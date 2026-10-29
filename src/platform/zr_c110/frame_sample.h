#ifndef FX_C110_FRAME_SAMPLE_H
#define FX_C110_FRAME_SAMPLE_H
#include "fenestra_observation.h"
#include "fenestra.h"
/* A saved native input copy is distinct from a sensor integration ID. */
#define FX_FS_BYTES 16384u
#define FX_FS_HEADER 256u
#define FX_FS_SLOTS 128u
#define FX_FS_PACKET 1u
#define FX_FS_NATIVE_ROWS 2u
#define FX_FS_ALLOW_NATIVE_LAYOUT 2u
#define FX_FS_WAIT_REC 4u
#define FX_FS_NATIVE_LEASE 16u
#define FX_FS_LEASE_HEADER 320u
#define FX_FS_FAULT_IMMUTABLE 768u
#define FX_FS_FAULT_BYTES 896u
#define FX_FS_JOURNAL 32u
enum { FX_FS_S_UNKNOWN,FX_FS_S_ROTATE,FX_FS_S_BANK_BEGIN,FX_FS_S_BANK_END,
    FX_FS_S_INPUT,FX_FS_S_IRQ,FX_FS_S_COMPLETION,FX_FS_S_REC_ENTRY,
    FX_FS_S_REC_ACCEPT,FX_FS_S_REC_EXIT,FX_FS_S_WORKER,FX_FS_S_REQUEST,
    FX_FS_S_RETAIN,FX_FS_S_COPY,FX_FS_S_RETURN,FX_FS_S_FREEZE,FX_FS_S_ADAPTER };
enum { FX_FS_P_LOCK=1,FX_FS_P_ROTATE_RANGE=10,FX_FS_P_ROTATE_OVERLAP,
    FX_FS_P_ROTATE_SLOTS,FX_FS_P_REUSE_HELD,FX_FS_P_BANK_NESTED=20,
    FX_FS_P_BANK_INDEX,FX_FS_P_BANK_EXIT,FX_FS_P_BANK_SLOT,FX_FS_P_BANK_POST,
    FX_FS_P_IRQ_STATUS=30,FX_FS_P_IRQ_WRAP,FX_FS_P_COMPLETION_ORDER=40,
    FX_FS_P_COMPLETION_ID,FX_FS_P_COMPLETION_REUSE,FX_FS_P_COMPLETION_BASELINE,
    FX_FS_P_REC_NESTED=50,FX_FS_P_REC_INFLIGHT,FX_FS_P_FREQUENCY=60,
    FX_FS_P_COPY_GEOMETRY=70,FX_FS_P_COPY_TIME,FX_FS_P_COPY_READ,
    FX_FS_P_COPY_MISMATCH,FX_FS_P_COPY_EPOCH,FX_FS_P_COPY_ELIGIBLE,FX_FS_P_COPY_ADMISSION,
    FX_FS_P_RETAIN_RESULT=80,FX_FS_P_RETURN_RESULT,FX_FS_P_RETURN_TIME,
    FX_FS_P_DRAIN_TIME,FX_FS_P_IRQ_OBJECT=90,FX_FS_P_IRQ_CONTEXT,
    FX_FS_P_IRQ_RETURN,FX_FS_P_IRQ_RECEIVER,FX_FS_P_IRQ_NOTIFICATION,
    FX_FS_P_IRQ_SCOPE,FX_FS_P_SOURCE_ENTRY,FX_FS_P_SOURCE_SCOPE,
    FX_FS_P_SOURCE_FLAGS,FX_FS_P_SOURCE_DESCRIPTOR,FX_FS_P_BANK_CLAIM,
    FX_FS_P_ROTATION_METADATA,FX_FS_P_CONSUMER_METADATA,FX_FS_P_FIFO_BOUNDARY,
    FX_FS_P_POOL_RESET,FX_FS_P_RETAIN_COUNT,FX_FS_P_RETAIN_LOOKUP,FX_FS_P_RETAIN_VERIFY,
    FX_FS_P_RETURN_TOKEN,FX_FS_P_INPUT_ORDER,FX_FS_P_INPUT_FULL,FX_FS_P_COPY_RESERVE,
    FX_FS_P_JOURNAL_FULL,FX_FS_P_JOURNAL_ORDER,
    FX_FS_P_GENERIC=255 };
typedef struct { uint64_t ticks,cpu,task,sp,kernel;uint32_t kind; } FxFrameContext;
typedef struct {
    uint64_t base,capacity,generation,data;
    uint32_t pending,index;
    uint64_t completion,completed,job,descriptor;
    uint64_t job_identity,descriptor_identity;
    uint64_t observed_completion;
    uint32_t completed_bank,completed_slot;
} FxFrameSlot;
typedef struct {
    uint64_t base,data,capacity,generation,job,descriptor,consumed;
    uint32_t source_type,native_type,format,width,height,stride,source_format;
    uint32_t input_word0,input_word1;
    uint64_t completion,completed;
    uint64_t job_identity,descriptor_identity;
    uint32_t completed_bank,completed_slot;
} FxFrameInput;
typedef struct {
    _Atomic uint64_t ready;
    uint64_t words[6];
    FxFrameContext producer;
    uint64_t site;
    uint32_t stage,generation,epoch;
} FxFrameEvent;
typedef struct {
    void *context;
    /* Both native controller and allocator must be excluded without waiting.
     * Never substitute an IRQ disable, native sleep or an unbounded lock. */
    /* 1 acquired; 0 unspecified busy; -2 controller busy; -3 pool busy;
     * -1 invalid native exclusion metadata (integrity failure). */
    int (*reserve)(void *);
    void (*unreserve)(void *);
    int (*read)(void *,uint64_t,uint32_t,uint8_t *,uint64_t *);
    /* Explicit original-pool retention. BUSY never means ownership released.
     * All callbacks run on the observer worker, never inside the IRQ. */
    FxResult (*retain)(void *,const FxFrameInput *,uint64_t *);
    FxResult (*restore)(void *,uint64_t);
    void (*fatal)(void *,FxResult);
} FxFrameIO;
/* One observer operation, retained even when its result cannot immediately
 * reacquire the short metadata lock. IRQ callbacks never access this record. */
typedef struct {
    FxFrameInput input;
    uint64_t token,begin,end,detail[4];
    uint32_t result,predicate,used,temporary,epoch,busy_reason;
} FxFrameWork;
typedef struct {
    _Atomic uint32_t lock,accepting,stop,callbacks,lost,writers,write_epoch,native_bank_claim,rec_started;
    _Atomic uint32_t phase;
    uint32_t reason,error,generation,jobs,duration_ms,flags,source,used;
    uint32_t x,y,width,height,offset,count,bank_known[2],slot_count,selected;
    uint64_t id,boot,activation,connection,frequency,start,end,deadline;
    uint64_t bank[2][4],serial;
    uint64_t bank_sp,bank_lr,bank_cpu,bank_entry[4];
    uint32_t bank_number,bank_valid;
    uint64_t rec_sp,rec_task,rec_ticks;
    FxFrameSlot slot[FX_FS_SLOTS];
    FxFrameInput candidate;
    uint8_t nonce[16],nonces[FX_OBS_JOBS][16],digest[32],data[FX_FS_BYTES];
    _Atomic uint32_t lease_owned,pool_reset,native_stop;
    uint32_t lease_fault,irq_status,irq_known,source_wait;
    uint32_t irq_mask,irq_notification,baseline_ready,source_pattern;
    uint64_t baseline_sequence,baseline_skipped,source_job_identity;
    _Atomic uint32_t irq_scope;
    uint64_t native_hold,hold_base,held_at,returned_at,return_started;
    uint64_t irq_device,irq_sp,irq_cpu,irq_sequence,irq_consumed,irq_ticks;
    uint64_t irq_address[2][4],irq_generation[2][4];
    _Atomic uint64_t source_sp;
    uint64_t source_task,source_job,source_completion;
    uint64_t reset_sp,reset_lr,reset_cpu;
    /* Diagnostics only: never used to manufacture source qualification.
     * Snapshot words are atomic so a failed CAS need not acquire this lock. */
    _Atomic uint32_t fault_state,shadow_sequence,owner_sequence;
    _Atomic uint32_t fault_irq_raw,fault_irq_mask,fault_irq_notification,fault_wait_pattern;
    _Atomic uint64_t shadow[40],owner[7];
    uint8_t fault_build[32],first_fault[FX_FS_FAULT_IMMUTABLE];
    uint64_t retain_begin,retain_end,copy_begin,copy_end,restore_begin,restore_end;
    uint32_t retain_result,restore_result,copy_result,copy_temporary_bytes;
    _Atomic uint32_t worker,operation;
    FxFrameWork work;
    /* Single bounded consumer handoff: 0 empty,1 publishing,2 ready,3 chosen.
     * Lifecycle observers remain active after selection. No native pointer is
     * dereferenced during deferred processing. */
    _Atomic uint32_t input_state,source_active;
    _Atomic uint64_t source_version;
    uint32_t input_generation;
    uint64_t input_version;
    FxFrameInput input_pending;
    uint64_t copy_attempts,copy_busy[9];
    uint32_t copy_busy_reason;
    _Atomic uint64_t journal_head,journal_drops,irq_announced;
    _Atomic uint32_t journal_poison,rec_announced;
    FxFrameEvent journal[FX_FS_JOURNAL];
    const FxFrameEvent *applying;
    uint64_t applying_sequence;
    uint32_t replay_epoch;
} FxFrameSample;
void fx_frame_sample_context(FxFrameContext *);
void fx_frame_sample_fault(FxFrameSample *,uint32_t,uint32_t,uint64_t,int,const uint64_t[12]);
void fx_frame_sample_init(FxFrameSample *);
int fx_frame_sample_begin(FxFrameSample *);
void fx_frame_sample_end(FxFrameSample *);
void fx_frame_sample_lost(FxFrameSample *);
void fx_frame_sample_cancel(FxFrameSample *,uint32_t);
int fx_frame_sample_quiesced(const FxFrameSample *);
int fx_frame_sample_can_retain(const FxFrameSample *);
void fx_frame_sample_rotate(FxFrameSample *,uint64_t,uint64_t,uint32_t);
void fx_frame_sample_bank_begin(FxFrameSample *,uint32_t);
void fx_frame_sample_bank_end(FxFrameSample *,uint32_t,const uint64_t[4],int);
void fx_frame_sample_input(FxFrameSample *,const FxFrameInput *);
void fx_frame_sample_input_at(FxFrameSample *,const FxFrameInput *,uint64_t);
void fx_frame_sample_step(FxFrameSample *,const FxObservationIO *,const FxFrameIO *,int);
uint32_t fx_frame_sample_request(FxFrameSample *,const FxObservationIO *,
    const uint8_t *,uint32_t,uint8_t *,uint32_t *);
int fx_frame_sample_valid_arm(const uint8_t *,uint32_t);
void fx_frame_sample_rec_entry(FxFrameSample *,uint64_t,uint64_t);
void fx_frame_sample_rec_accept(FxFrameSample *,uint64_t,uint64_t,uint64_t);
void fx_frame_sample_rec_exit(FxFrameSample *,uint64_t,uint64_t);
void fx_frame_sample_rec_exit_at(FxFrameSample *,uint64_t,uint64_t,uint64_t);
void fx_frame_sample_irq(FxFrameSample *,uint32_t,uint64_t);
void fx_frame_sample_completion(FxFrameSample *,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
#endif

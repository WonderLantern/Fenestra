/* Camera-side Linux debug worker. Normal syscalls, no privilege escalation.
 * Native transport maps a dedicated allocation through Nikon's IPC device.
 * A separately compiled stdio fixture exercises the same request dispatcher.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

#define LIMIT 1024u
#define JOBS 4u
#define FILES 8u
#define RING 16384u
#define SHARED 8192u
enum { INFO=1, EXEC_ARGV, SHELL_OPEN, JOB_STATUS, JOB_CANCEL, JOB_CLOSE,
       STREAM_READ, STREAM_WRITE, SHELL_RESIZE, FILE_OPEN, FILE_READ,
       FILE_WRITE, FILE_CLOSE, FILE_STAT, MEMORY_READ, MEMORY_WRITE, MAPS_READ,
       STDIN_CLOSE, FILE_SYNC };
typedef struct { uint8_t data[RING];uint64_t end; } Ring;
typedef struct {
    uint32_t id,pty,done,timed_out;pid_t pid;int input,output,error,status;
    uint64_t deadline,cancel_at;Ring streams[2];
} Job;
typedef struct { uint32_t id;int fd; } File;
static Job jobs[JOBS];
static File files[FILES];
static uint32_t next_id=1;
static uint64_t milliseconds(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) { return get32(p)|(uint64_t)get32(p+4)<<32; }
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
static void put64(uint8_t *p,uint64_t v) { put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32)); }
static void close_fd(int *fd) { if(*fd>=0)close(*fd);*fd=-1; }
static Job *job(uint32_t id) { for(unsigned i=0;i<JOBS;++i)if(id && jobs[i].id==id)return &jobs[i];return NULL; }
static File *file(uint32_t id) { for(unsigned i=0;i<FILES;++i)if(id && files[i].id==id)return &files[i];return NULL; }
static uint32_t new_id(void) { if(!next_id || next_id==UINT32_MAX)return 0;return next_id++; }
static void signal_job(Job *j,int sig) {
    if(!j->done && j->pid>0) { kill(-j->pid,sig);kill(j->pid,sig); }
}
static void drain(int *fd,Ring *r) {
    if(*fd<0)return;
    uint8_t b[1024];
    for(unsigned round=0;round<4;++round) {
        ssize_t n=read(*fd,b,sizeof(b));
        if(n>0) { for(ssize_t i=0;i<n;++i)r->data[(r->end++)%RING]=b[i]; }
        else if(n==0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) { close_fd(fd);break; }
        else break;
    }
}
static void pump(void) {
    uint64_t now=milliseconds();
    for(unsigned i=0;i<JOBS;++i) {
        Job *j=&jobs[i];if(!j->id)continue;
        drain(&j->output,&j->streams[0]);drain(&j->error,&j->streams[1]);
        if(j->done)continue;
        if(!j->cancel_at && now>=j->deadline) { j->timed_out=1;j->cancel_at=now;signal_job(j,SIGTERM); }
        if(j->cancel_at && now-j->cancel_at>=200u)signal_job(j,SIGKILL);
        siginfo_t info;memset(&info,0,sizeof(info));
        if(waitid(P_PID,(id_t)j->pid,&info,WEXITED|WNOHANG|WNOWAIT)==0 && info.si_pid==j->pid) {
            /* Keep the leader unreaped while cleaning its process group: its
             * PID cannot be recycled into an unrelated process in this window. */
            signal_job(j,SIGKILL);
            if(waitpid(j->pid,&j->status,WNOHANG)==j->pid) {
                j->done=1;close_fd(&j->input);
                drain(&j->output,&j->streams[0]);drain(&j->error,&j->streams[1]);
            }
        }
    }
}
static void shutdown_owned(void) {
    for(unsigned i=0;i<JOBS;++i)if(jobs[i].id)signal_job(&jobs[i],SIGKILL);
    for(unsigned i=0;i<JOBS;++i)if(jobs[i].id) {
        Job *j=&jobs[i];if(!j->done)while(waitpid(j->pid,&j->status,0)<0 && errno==EINTR){}
        close_fd(&j->input);close_fd(&j->output);close_fd(&j->error);
    }
    for(unsigned i=0;i<FILES;++i)if(files[i].id)close_fd(&files[i].fd);
}
static int make_job(char *const argv[],int pty,uint32_t timeout,Job **created) {
    if(timeout<100u || timeout>300000u || !argv[0] || argv[0][0]!='/')return EINVAL;
    Job *j=NULL;for(unsigned i=0;i<JOBS;++i)if(!jobs[i].id){j=&jobs[i];break;}
    if(!j)return EBUSY;
    int in[2]={-1,-1},out[2]={-1,-1},err[2]={-1,-1};int master=-1,slave=-1,result=0;
    uint32_t id=new_id();if(!id)return EOVERFLOW;
    if(pty) {
        char name[128];master=posix_openpt(O_RDWR|O_NOCTTY|O_CLOEXEC|O_NONBLOCK);
        if(master<0 || grantpt(master) || unlockpt(master)) {result=errno;goto fail;}
        result=ptsname_r(master,name,sizeof(name));if(result)goto fail;
        slave=open(name,O_RDWR|O_NOCTTY|O_CLOEXEC);if(slave<0){result=errno;goto fail;}
        in[0]=slave;slave=-1;out[1]=dup(in[0]);err[1]=dup(in[0]);
        if(out[1]<0 || err[1]<0){result=errno;goto fail;}
        fcntl(out[1],F_SETFD,FD_CLOEXEC);fcntl(err[1],F_SETFD,FD_CLOEXEC);
    } else if(pipe2(in,O_CLOEXEC|O_NONBLOCK) || pipe2(out,O_CLOEXEC|O_NONBLOCK) || pipe2(err,O_CLOEXEC|O_NONBLOCK)) { result=errno;goto fail; }
    pid_t pid=fork();
    if(pid<0){result=errno;goto fail;}
    if(pid==0) {
        if(setsid()<0)_exit(126);
        if(pty && ioctl(in[0],TIOCSCTTY,0)<0)_exit(126);
        if(dup2(in[0],0)<0 || dup2(out[1],1)<0 || dup2(err[1],2)<0)_exit(126);
        for(int fd=0;fd<3;++fd){int f=fcntl(fd,F_GETFL);if(f>=0)fcntl(fd,F_SETFL,f&~O_NONBLOCK);}
        /* All other owned descriptors have CLOEXEC. */
        signal(SIGPIPE,SIG_DFL);execv(argv[0],argv);_exit(errno==ENOENT?127:126);
    }
    memset(j,0,sizeof(*j));j->id=id;j->pid=pid;j->pty=(uint32_t)pty;j->deadline=milliseconds()+timeout;
    j->input=j->output=j->error=-1;
    close_fd(&in[0]);close_fd(&out[1]);close_fd(&err[1]);
    if(pty) { j->input=fcntl(master,F_DUPFD_CLOEXEC,3);j->output=master;master=-1;
        if(j->input<0) { result=errno;signal_job(j,SIGKILL);waitpid(pid,NULL,0);close_fd(&j->output);j->id=0;goto fail; } }
    else { j->input=in[1];in[1]=-1;j->output=out[0];out[0]=-1;j->error=err[0];err[0]=-1; }
    *created=j;
fail:
    for(unsigned i=0;i<2;++i){close_fd(&in[i]);close_fd(&out[i]);close_fd(&err[i]);}
    close_fd(&master);close_fd(&slave);return result;
}
static int path_arg(const uint8_t *in,uint32_t n,char out[LIMIT+1]) {
    if(!n || n>LIMIT || in[0]!='/' || memchr(in,0,n))return EINVAL;
    memcpy(out,in,n);out[n]=0;return 0;
}
static int range_ok(uint64_t address,uint32_t n) { return n && n<=LIMIT-32 && address<=UINT64_MAX-n && address<INT64_MAX && address+n<=INT64_MAX; }
/* Request: op:u32 followed by operation-specific fields. Response starts with
 * errno:u32 and echoes op:u32. All errors are terminal request results. */
static uint32_t dispatch(const uint8_t *in,uint32_t bytes,uint8_t out[LIMIT]) {
    memset(out,0,LIMIT);uint32_t op=bytes>=4?get32(in):0,n=8;int e=0;
    put32(out+4,op);if(bytes<4){e=EINVAL;goto done;}
    in+=4;bytes-=4;
    if(op==INFO) {
        if(bytes){e=EINVAL;goto done;}
        memcpy(out+8,"FXL1",4);put32(out+12,1);put32(out+16,(uint32_t)getpid());
        put32(out+20,(uint32_t)getuid());put32(out+24,(uint32_t)getgid());
        put32(out+28,JOBS);put32(out+32,FILES);put32(out+36,RING);
        put32(out+40,0x7ffffu);put32(out+44,0); /* privileged; Flash prevention unverified */
        if(!getcwd((char *)out+48,LIMIT-48)){e=errno;goto done;}
        n=48u+(uint32_t)strlen((char *)out+48)+1u;
    } else if(op==EXEC_ARGV || op==SHELL_OPEN) {
        if(bytes<4){e=EINVAL;goto done;}uint32_t timeout=get32(in);Job *j=NULL;
        char storage[LIMIT+1],*argv[34];
        if(op==SHELL_OPEN) {
            if(bytes!=4){e=EINVAL;goto done;}
            argv[0]="/bin/sh";argv[1]="-i";argv[2]=NULL;
        } else {
            if(bytes<8){e=EINVAL;goto done;}uint32_t count=get32(in+4);
            if(!count || count>32 || bytes==8 || in[bytes-1]){e=EINVAL;goto done;}
            memcpy(storage,in+8,bytes-8);uint32_t offset=0;
            for(uint32_t i=0;i<count;++i) {
                if(offset>=bytes-8){e=EINVAL;goto done;}argv[i]=storage+offset;
                offset+=(uint32_t)strlen(storage+offset)+1u;
            }
            if(offset!=bytes-8){e=EINVAL;goto done;}argv[count]=NULL;
        }
        e=make_job(argv,op==SHELL_OPEN,timeout,&j);
        if(!e){put32(out+8,j->id);put32(out+12,(uint32_t)j->pid);put32(out+16,j->pty);n=20;}
    } else if(op>=JOB_STATUS && op<=SHELL_RESIZE) {
        if(bytes<4){e=EINVAL;goto done;}Job *j=job(get32(in));if(!j){e=ESTALE;goto done;}
        if(op==JOB_STATUS) {
            if(bytes!=4){e=EINVAL;goto done;}
            put32(out+8,j->id);put32(out+12,j->done && j->output<0 && j->error<0);put32(out+16,(uint32_t)j->pid);
            put32(out+20,j->done && WIFEXITED(j->status)?(uint32_t)WEXITSTATUS(j->status):UINT32_MAX);
            put32(out+24,j->done && WIFSIGNALED(j->status)?(uint32_t)WTERMSIG(j->status):0);
            put32(out+28,j->timed_out);put64(out+32,j->streams[0].end);put64(out+40,j->streams[1].end);n=48;
        } else if(op==JOB_CANCEL) {
            if(bytes!=4){e=EINVAL;goto done;}
            if(!j->done){if(!j->cancel_at)j->cancel_at=milliseconds();signal_job(j,SIGTERM);}
        } else if(op==JOB_CLOSE) {
            if(bytes!=4){e=EINVAL;goto done;}if(!j->done || j->output>=0 || j->error>=0){e=EBUSY;goto done;}
            close_fd(&j->input);close_fd(&j->output);close_fd(&j->error);memset(j,0,sizeof(*j));
        } else if(op==STREAM_READ) {
            if(bytes!=20){e=EINVAL;goto done;}uint32_t which=get32(in+4),count=get32(in+16);uint64_t offset=get64(in+8);
            if(which>1 || !count || count>LIMIT-32){e=EINVAL;goto done;}
            Ring *r=&j->streams[which];uint64_t base=r->end>RING?r->end-RING:0;
            put64(out+8,base);put64(out+16,r->end);put32(out+24,(which?j->error:j->output)<0);n=32;
            if(offset<base){e=ESTALE;goto done;}if(offset>r->end){e=EINVAL;goto done;}
            if((uint64_t)count>r->end-offset)count=(uint32_t)(r->end-offset);
            for(uint32_t i=0;i<count;++i)out[32+i]=r->data[(offset+i)%RING];
            put32(out+28,count);n+=count;
        } else if(op==STREAM_WRITE) {
            if(bytes==4 || j->done || j->input<0){e=EPIPE;goto done;}
            ssize_t wrote=write(j->input,in+4,bytes-4);
            if(wrote<0)e=errno;else{put32(out+8,(uint32_t)wrote);n=12;}
        } else {
            if(bytes!=12 || !j->pty || j->done || j->input<0){e=EINVAL;goto done;}
            uint32_t rows=get32(in+4),cols=get32(in+8);
            if(!rows || rows>4096 || !cols || cols>4096){e=EINVAL;goto done;}
            struct winsize size={(unsigned short)rows,(unsigned short)cols,0,0};
            if(ioctl(j->input,TIOCSWINSZ,&size)<0)e=errno;
        }
    } else if(op==STDIN_CLOSE) {
        if(bytes!=4){e=EINVAL;goto done;}Job *j=job(get32(in));if(!j){e=ESTALE;goto done;}
        /* A PTY master cannot be half-closed: closing its input duplicate does
         * not deliver EOF while output still owns the master. Send terminal
         * input (e.g. canonical VEOF) through STREAM_WRITE instead. */
        if(j->pty){e=ENOTSUP;goto done;}
        int fd=j->input;j->input=-1;
        if(fd>=0 && close(fd)<0)e=errno; /* never retry close after EINTR */
    } else if(op==FILE_SYNC) {
        if(bytes!=4){e=EINVAL;goto done;}File *f=file(get32(in));if(!f){e=ESTALE;goto done;}
        if(fdatasync(f->fd)<0)e=errno;
    } else if(op==FILE_OPEN || op==FILE_STAT) {
        uint32_t flags=0;char path[LIMIT+1];
        if(op==FILE_OPEN){if(bytes<8){e=EINVAL;goto done;}flags=get32(in);in+=4;bytes-=4;}
        if((e=path_arg(in,bytes,path)))goto done;
        if(op==FILE_STAT) {
            struct stat st;if(lstat(path,&st)<0){e=errno;goto done;}
            put32(out+8,(uint32_t)st.st_mode);put32(out+12,(uint32_t)st.st_uid);put32(out+16,(uint32_t)st.st_gid);
            put64(out+24,(uint64_t)st.st_size);n=32;
        } else {
            /* Modes: 0 read existing;1 read/write existing;2 exclusive create.
             * No hidden truncate, special device, FIFO, or symlink open. */
            if(flags>2){e=EINVAL;goto done;}File *f=NULL;
            for(unsigned i=0;i<FILES;++i)if(!files[i].id){f=&files[i];break;}
            if(!f){e=EMFILE;goto done;}uint32_t id=new_id();if(!id){e=EOVERFLOW;goto done;}
            int fd=open(path,O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK|(flags?O_RDWR:O_RDONLY)|(flags==2?O_CREAT|O_EXCL:0),0600);
            if(fd<0){e=errno;goto done;}struct stat st;
            if(fstat(fd,&st)<0){e=errno;close(fd);goto done;}
            if(!S_ISREG(st.st_mode)){e=ENOTSUP;close(fd);goto done;}
            f->id=id;f->fd=fd;put32(out+8,id);put64(out+12,(uint64_t)st.st_size);n=20;
        }
    } else if(op>=FILE_READ && op<=FILE_CLOSE) {
        if(bytes<4){e=EINVAL;goto done;}File *f=file(get32(in));if(!f){e=ESTALE;goto done;}
        if(op==FILE_CLOSE) { if(bytes!=4){e=EINVAL;goto done;}int fd=f->fd;f->id=0;f->fd=-1;if(close(fd)<0)e=errno; }
        else {
            if(bytes<16){e=EINVAL;goto done;}uint64_t offset=get64(in+4);uint32_t count=get32(in+12);
            if(!range_ok(offset,count) || (op==FILE_READ?bytes!=16:bytes!=16+count)){e=EINVAL;goto done;}
            ssize_t got=op==FILE_READ?pread(f->fd,out+12,count,(off_t)offset):pwrite(f->fd,in+16,count,(off_t)offset);
            if(got<0)e=errno;else {put32(out+8,(uint32_t)got);n=12u+(op==FILE_READ?(uint32_t)got:0u);}
        }
    } else if(op==MEMORY_READ || op==MEMORY_WRITE || op==MAPS_READ) {
        if(bytes<16){e=EINVAL;goto done;}uint32_t pid=get32(in),count=get32(in+12);uint64_t address=get64(in+4);
        if(!pid || pid>INT32_MAX || !range_ok(address,count) || (op==MEMORY_WRITE?bytes!=16+count:bytes!=16)){e=EINVAL;goto done;}
        ssize_t got;
        if(op==MAPS_READ) {
            char path[64];snprintf(path,sizeof(path),"/proc/%u/maps",pid);int fd=open(path,O_RDONLY|O_CLOEXEC);
            if(fd<0){e=errno;goto done;}got=pread(fd,out+12,count,(off_t)address);int saved=errno;close(fd);errno=saved;
        } else {
            struct iovec local={op==MEMORY_READ?(void *)(out+12):(void *)(in+16),count};
            struct iovec remote={(void *)(uintptr_t)address,count};
            got=op==MEMORY_READ?process_vm_readv((pid_t)pid,&local,1,&remote,1,0):process_vm_writev((pid_t)pid,&local,1,&remote,1,0);
        }
        if(got<0)e=errno;else {put32(out+8,(uint32_t)got);n=12u+(op==MEMORY_WRITE?0u:(uint32_t)got);}
    } else e=ENOSYS;
done:
    put32(out,(uint32_t)e);return n;
}

#ifdef FX_LINUX_STDIO_FIXTURE
static int exact(int fd,void *buffer,size_t count) {
    uint8_t *p=buffer;while(count){ssize_t n=read(fd,p,count);if(n<=0)return -1;p+=n;count-=(size_t)n;}return 0;
}
int main(void) {
    signal(SIGPIPE,SIG_IGN);
    for(;;) {
        pump();struct pollfd p={0,POLLIN,0};int ready=poll(&p,1,10);
        if(ready<0 && errno==EINTR)continue;
        if(ready<=0)continue;
        if(!(p.revents&POLLIN))break;
        uint8_t b[LIMIT],o[LIMIT],size[4];if(exact(0,size,4))break;
        uint32_t n=get32(size);if(n>LIMIT || exact(0,b,n))break;
        uint32_t length=dispatch(b,n,o);put32(size,length);
        if(write(1,size,4)!=4 || write(1,o,length)!=(ssize_t)length)break;
    }
    shutdown_owned();return 0;
}
#else
int main(int argc,char **argv) {
#ifdef FX_LINUX_SHARED_FIXTURE
    if(argc!=5)return 64;
#else
    if(argc!=4)return 64;
#endif
    char *end;errno=0;uint64_t physical=strtoull(argv[1],&end,16);if(errno || *end || (physical&4095u) || physical>INT64_MAX-SHARED)return 64;
    errno=0;uint64_t nonce=strtoull(argv[2],&end,16);if(errno || *end || !nonce)return 64;
    /* This selects the shared-memory ABI, not the camera model. The historical
     * C1.10 token remains accepted for existing ZR firmware and clients. */
    if(strcmp(argv[3],"FXL3") && strcmp(argv[3],"C1.10"))return 64;
#ifdef FX_LINUX_SHARED_FIXTURE
    int fd=open(argv[4],O_RDWR|O_CLOEXEC);physical=0;
#else
    /* Both original LaMainApp implementations map shared physical pages via
     * oscom_memhelper. nikon_ipcu is a different IPC endpoint, not this ABI. */
    int fd=open("/dev/oscom_memhelper",O_RDWR|O_CLOEXEC);
#endif
    if(fd<0)return 65;
    uint8_t *shared=mmap(NULL,SHARED,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t)physical);close(fd);
    if(shared==MAP_FAILED)return 65;
    if(memcmp(shared,"FXL3",4) || get32(shared+4)!=SHARED || get64(shared+8)!=nonce){munmap(shared,SHARED);return 66;}
    signal(SIGPIPE,SIG_IGN);
    _Atomic uint32_t *req=(_Atomic uint32_t *)(shared+128),*res=(_Atomic uint32_t *)(shared+2048);
    _Atomic uint32_t *stop=(_Atomic uint32_t *)(shared+72),*state=(_Atomic uint32_t *)(shared+4128);
    _Atomic uint64_t *host=(_Atomic uint64_t *)(shared+64),*alive=(_Atomic uint64_t *)(shared+4096);
    uint64_t heartbeat=atomic_load_explicit(host,memory_order_acquire),seen=milliseconds();uint32_t sequence=0;
    put32(shared+4104,(uint32_t)getpid());put32(shared+4108,(uint32_t)getuid());put32(shared+4112,(uint32_t)getgid());
    atomic_store_explicit(state,1,memory_order_release);
    while(!atomic_load_explicit(stop,memory_order_acquire)) {
        uint64_t now=milliseconds(),value=atomic_load_explicit(host,memory_order_acquire);
        if(value!=heartbeat){heartbeat=value;seen=now;}if(now-seen>2000u)break;
        atomic_store_explicit(alive,now,memory_order_release);pump();
        uint32_t current=atomic_load_explicit(req,memory_order_acquire);
        if(current && current!=sequence) {
            uint32_t n=get32(shared+132);uint8_t input[LIMIT],output[LIMIT];
            if(n>LIMIT){put32(output,EINVAL);put32(output+4,0);n=8;}
            else{memcpy(input,shared+136,n);n=dispatch(input,n,output);}
            memcpy(shared+2056,output,n);put32(shared+2052,n);sequence=current;
            atomic_store_explicit(res,sequence,memory_order_release);
        }
        struct timespec pause={0,10000000};nanosleep(&pause,NULL);
    }
    shutdown_owned();atomic_store_explicit(state,2,memory_order_release);munmap(shared,SHARED);return 0;
}
#endif

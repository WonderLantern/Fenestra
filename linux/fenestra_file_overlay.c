/* Publish one verified RAM file over an existing path in this mount
 * namespace. All output files are created on a verified RAM filesystem.
 * Privileges come from the caller. The kernel enforces mount permissions. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include "fenestra_sha256.h"

#define TMPFS_MAGIC 0x01021994L
#define RAMFS_MAGIC 0x858458f6L

static int fail(const char *step) {
    fprintf(stderr,"fenestra-file-overlay: %s: %s\n",step,strerror(errno));
    return 1;
}

static int proc_path(char *out,size_t capacity,int fd,const char *suffix) {
    int length=snprintf(out,capacity,"/proc/self/fd/%d%s",fd,suffix);
    if(length<0 || (size_t)length>=capacity){errno=ENAMETOOLONG;return -1;}
    return 0;
}

static int regular(int fd,struct stat *out) {
    if(fstat(fd,out))return -1;
    if(!S_ISREG(out->st_mode)){errno=EINVAL;return -1;}
    return 0;
}

static int parse_digest(const char *text,uint8_t out[32]) {
    if(strlen(text)!=64u)return -1;
    for(size_t i=0;i<32u;++i) {
        unsigned value=0u;
        for(size_t j=0;j<2u;++j) {
            unsigned char c=(unsigned char)text[2u*i+j];
            unsigned digit;
            if(c>='0' && c<='9')digit=(unsigned)(c-'0');
            else if(c>='a' && c<='f')digit=(unsigned)(c-'a')+10u;
            else if(c>='A' && c<='F')digit=(unsigned)(c-'A')+10u;
            else return -1;
            value=value*16u+digit;
        }
        out[i]=(uint8_t)value;
    }
    return 0;
}

static int ram_storage(int fd) {
    struct statfs fs;
    if(fstatfs(fd,&fs))return -1;
    if((unsigned long)fs.f_type==RAMFS_MAGIC)return 0;
    if((unsigned long)fs.f_type!=TMPFS_MAGIC){errno=EROFS;return -1;}
    /* Linux 4.14 lacks the later tmpfs noswap mount option. */
    FILE *swaps=fopen("/proc/swaps","re");
    if(!swaps)return -1;
    char line[4096];
    int error=0;
    if(!fgets(line,sizeof(line),swaps))error=EIO;
    else if(fgets(line,sizeof(line),swaps))error=EBUSY;
    if(ferror(swaps))error=EIO;
    if(fclose(swaps) && !error)error=errno;
    if(error){errno=error;return -1;}
    return 0;
}

static int copy_verified(int source,int destination,const uint8_t expected[32],uint64_t limit) {
    uint8_t data[65536],actual[32];
    uint64_t copied=0u;
    FxSha256 hash;
    fx_sha256_init(&hash);
    for(;;) {
        ssize_t bytes=read(source,data,sizeof(data));
        if(bytes<0){if(errno==EINTR)continue;return -1;}
        if(!bytes)break;
        if((uint64_t)bytes>limit-copied){errno=EFBIG;return -1;}
        copied+=(uint64_t)bytes;
        if(fx_sha256_update(&hash,data,(size_t)bytes)!=FX_OK){errno=EOVERFLOW;return -1;}
        size_t offset=0u;
        while(offset<(size_t)bytes) {
            ssize_t done=write(destination,data+offset,(size_t)bytes-offset);
            if(done<0){if(errno==EINTR)continue;return -1;}
            if(!done){errno=EIO;return -1;}
            offset+=(size_t)done;
        }
    }
    fx_sha256_final(&hash,actual);
    if(memcmp(actual,expected,32u)){errno=EBADMSG;return -1;}
    return 0;
}

int main(int argc,char **argv) {
    if(argc!=6 && argc!=7) {
        fputs("Usage: fenestra-file-overlay SOURCE TARGET SHA256 RAM_DIRECTORY [MAX_BYTES]\n",stderr);
        return 2;
    }
    uint8_t expected[32];
    uint64_t limit=UINT64_C(67108864);
    if(parse_digest(argv[3],expected)){errno=EINVAL;return fail("replacement SHA-256");}
    if(argc==7) {
        char *end;
        errno=0;
        unsigned long long value=strtoull(argv[6],&end,10);
        if(errno || !*argv[6] || argv[6][0]=='-' || *end || !value) {
            errno=EINVAL;
            return fail("MAX_BYTES");
        }
        limit=(uint64_t)value;
    }
    if(argv[1][0]!='/' || argv[2][0]!='/' || argv[4][0]!='/') {
        errno=EINVAL;
        return fail("paths must be absolute");
    }
    int source=-1,target=-1,root=-1,directory=-1,payload=-1;
    int directory_mounted=0,file_mounted=0,published=0,result=1;
    const char *step="open source";
    char root_path[64],directory_path[128]="",file_path[96]="",target_path[64],name[48]="";
    struct stat original,source_stat;
    source=open(argv[1],O_RDONLY|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW|O_NOATIME);
    if(source<0 || regular(source,&source_stat))goto cleanup;
    step="open original";
    target=open(argv[2],O_RDONLY|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW|O_NOATIME);
    if(target<0 || regular(target,&original))goto cleanup;
    step="open RAM directory";
    root=open(argv[4],O_PATH|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(root<0 || ram_storage(root))goto cleanup;
    step="create private staging directory";
    if(proc_path(root_path,sizeof(root_path),root,""))goto cleanup;
    int length=snprintf(directory_path,sizeof(directory_path),"%s/fenestra-overlay-XXXXXX",root_path);
    if(length<0 || (size_t)length>=sizeof(directory_path)){errno=ENAMETOOLONG;goto cleanup;}
    if(!mkdtemp(directory_path)){directory_path[0]=0;goto cleanup;}
    const char *basename=strrchr(directory_path,'/');
    if(!basename || strlen(basename+1u)>=sizeof(name)){errno=ENAMETOOLONG;goto cleanup;}
    strcpy(name,basename+1u);
    /* A private staging mount permits moving the completed file mount even
     * when the caller's RAM directory has shared propagation. */
    step="bind staging directory";
    if(mount(directory_path,directory_path,NULL,MS_BIND,NULL))goto cleanup;
    directory_mounted=1;
    step="make staging private";
    if(mount(NULL,directory_path,NULL,MS_PRIVATE,NULL))goto cleanup;
    directory=open(directory_path,O_PATH|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(directory<0 || ram_storage(directory))goto cleanup;
    step="create RAM snapshot";
    payload=openat(directory,"payload",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(payload<0)goto cleanup;
    step="copy and verify replacement";
    if(copy_verified(source,payload,expected,limit))goto cleanup;
    step="set snapshot ownership";
    if(fchown(payload,original.st_uid,original.st_gid) || fchmod(payload,original.st_mode&0777u))goto cleanup;
    step="close snapshot";
    if(close(payload)){payload=-1;goto cleanup;}
    payload=-1;
    if(proc_path(file_path,sizeof(file_path),directory,"/payload") ||
       proc_path(target_path,sizeof(target_path),target,""))goto cleanup;
    step="bind RAM snapshot";
    if(mount(file_path,file_path,NULL,MS_BIND,NULL))goto cleanup;
    file_mounted=1;
    step="make snapshot read-only";
    if(mount(NULL,file_path,NULL,MS_BIND|MS_REMOUNT|MS_RDONLY|MS_NODEV|MS_NOSUID,NULL))goto cleanup;
    step="publish RAM snapshot";
    if(mount(file_path,target_path,NULL,MS_MOVE,NULL))goto cleanup;
    file_mounted=0;
    published=1;
    result=0;
cleanup:
    if(result) {
        int saved=errno;
        (void)fail(step);
        errno=saved;
    }
    if(payload>=0)close(payload);
    if(file_mounted && umount2(file_path,MNT_DETACH))
        result=fail("remove staging file mount");
    if(directory>=0) {
        if(unlinkat(directory,"payload",0) && errno!=ENOENT)
            result=fail("remove staging file");
        close(directory);
    }
    if(directory_mounted && umount2(directory_path,MNT_DETACH))
        result=fail("remove staging directory mount");
    if(root>=0 && name[0] && unlinkat(root,name,AT_REMOVEDIR))
        result=fail("remove staging directory");
    if(root>=0)close(root);
    if(source>=0)close(source);
    if(target>=0)close(target);
    if(published)fputs("Published read-only RAM replacement in the current mount namespace.\n",stdout);
    return result;
}

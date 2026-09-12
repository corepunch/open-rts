#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void fail(const char *s) { perror(s); exit(1); }
static const struct { const char *game,*root,*extension; } games[] = {
    {"dark-colony","data/DCOLONY/SCENARIO",".MAP"},
    {"dark-reign","data/REIGN/dark/scenario",".SCN"},
    {"7legion","data/7LEGION","MAPT.000"},
    {"kknd","data/KKND/LEVELS/640",".LVL"},
};
static char **paths;
static size_t count;
static void scan(const char *root,const char *suffix) {
    DIR *d=opendir(root); if(!d) fail(root);
    struct dirent *e;
    while((e=readdir(d))) {
        if(e->d_name[0]=='.') continue;
        char path[2048];
        if(snprintf(path,sizeof(path),"%s/%s",root,e->d_name)>=(int)sizeof(path)) { errno=ENAMETOOLONG;fail(root); }
        struct stat st; if(lstat(path,&st))fail(path);
        if(S_ISDIR(st.st_mode))scan(path,suffix);
        else if(S_ISREG(st.st_mode) && strlen(path)>=strlen(suffix) &&
                !strcmp(path+strlen(path)-strlen(suffix),suffix)) {
            char **next=realloc(paths,(count+1)*sizeof(*paths)); if(!next)fail("realloc");
            paths=next; paths[count]=strdup(path); if(!paths[count++])fail("strdup");
        }
    }
    closedir(d);
}
static int compare(const void *a,const void *b) { return strcmp(*(char *const *)a,*(char *const *)b); }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void manifest(FILE *out,int game,int sprites) {
    if(sprites && game==3) { for(int i=0;i<100;++i)fprintf(out,"%d.mobd\n",i);return; }
    scan(sprites?(game==1?"data/REIGN/dark/graphics":"data/7LEGION/GFX"):games[game].root,
         sprites?(game==1?".FTG":".BIM"):games[game].extension);
    qsort(paths,count,sizeof(*paths),compare);
    for(size_t i=0;i<count;++i) {
        if(sprites && game==1) {
            FILE *in=fopen(paths[i],"rb");if(!in)fail(paths[i]);
            unsigned char header[12],record[36];
            if(fread(header,1,12,in)==12 && !memcmp(header,"BOTG",4)) {
                if(fseek(in,0,SEEK_END))fail(paths[i]);
                long length=ftell(in);
                uint32_t offset=le32(header+4),entries=le32(header+8);
                if(length<0 || offset>(unsigned long)length || entries>((unsigned long)length-offset)/36) { errno=EINVAL;fail(paths[i]); }
                if(fseek(in,offset,SEEK_SET))fail(paths[i]);
                for(uint32_t j=0;j<entries;++j) {
                    if(fread(record,1,36,in)!=36)fail(paths[i]);
                    char name[29];memcpy(name,record,28);name[28]=0;
                    size_t n=strlen(name);
                    if(n>=4 && (!strcmp(name+n-4,".spr")||!strcmp(name+n-4,".SPR")))
                        fprintf(out,"%s|%u,%u\n",paths[i],le32(record+28),le32(record+32));
                }
            }
            fclose(in);
        } else if(!sprites || strncmp(strrchr(paths[i],'/')+1,"TILES",5)) fprintf(out,"%s\n",paths[i]);
        free(paths[i]);
    }
    free(paths);paths=NULL;count=0;
}
static void run(char *const args[],const char *output,const char *errors) {
    pid_t pid=fork();if(pid<0)fail("fork");
    if(!pid) {
        if(output && !freopen(output,"w",stdout))fail(output);
        if(errors && !freopen(errors,"w",stderr))fail(errors);
        execvp(args[0],args);fail(args[0]);
    }
    int status;
    if(waitpid(pid,&status,0)<0)fail("waitpid");
    if(!WIFEXITED(status)||WEXITSTATUS(status))exit(1);
}
int main(int argc,char **argv) {
    char *source=".",*output=NULL;int fixtures=0,build=1;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--source-tree") && i+1<argc)source=argv[++i];
        else if(!strcmp(argv[i],"--output") && i+1<argc)output=argv[++i];
        else if(!strcmp(argv[i],"--fixtures"))fixtures=1;
        else if(!strcmp(argv[i],"--no-build"))build=0;
        else { fprintf(stderr,"usage: %s [--source-tree DIR] [--no-build] --fixtures | --output DIR\n",argv[0]);return 1; }
    }
    if(!fixtures && !output) { fputs("--output is required for catalogs\n",stderr);return 1; }
    if(output && mkdir(output,0755) && errno!=EEXIST)fail(output);
    setenv("SDL_VIDEODRIVER","dummy",1);
    if(build)run((char *const[]){"make","-C",source,"loader-catalogs",NULL},"/dev/null",NULL);
    for(int game=0;game<4;++game) {
        char binary[2048];snprintf(binary,sizeof(binary),"%s/build/bin/loader-%s",source,games[game].game);
        if(fixtures) { run((char *const[]){binary,"--fixtures",NULL},NULL,NULL);continue; }
        for(int sprites=0;sprites<2;++sprites) {
            if(sprites && game==0)continue;
            char path[2048],result[2048],errors[2048];
            char *kind=sprites?"sprites":"maps";
            snprintf(path,sizeof(path),"%s/%s-%s.manifest",output,games[game].game,kind);
            snprintf(result,sizeof(result),"%s/%s-%s.txt",output,games[game].game,kind);
            snprintf(errors,sizeof(errors),"%s/%s-%s.log",output,games[game].game,kind);
            FILE *out=fopen(path,"w");if(!out)fail(path);manifest(out,game,sprites);if(fclose(out))fail(path);
            run((char *const[]){binary,kind,path,NULL},result,errors);
            printf("%s %s -> %s\n",games[game].game,kind,result);
        }
    }
    return 0;
}

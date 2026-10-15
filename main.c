/* Name: Anjali Chand | Roll No: 09 | Project: SQL Parser and Query Executor */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "executor.h"
#include "tokenizer.h"
#include "buffer/buffer.h"
#include "transactions/txn.h"
#include "sessions/sessions.h"

static pthread_mutex_t engine_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int stop_monitor = 0;
static time_t started_at;
static char status_path[512] = "status.json";

static int starts_with_ci(const char *s,const char *prefix){while(*prefix){if(!*s||toupper((unsigned char)*s)!=toupper((unsigned char)*prefix))return 0;s++;prefix++;}return 1;}
static void write_status(void){long hits=0,misses=0;buffer_stats(&hits,&misses);session_write_status(status_path,(int)getpid(),(long)(time(NULL)-started_at),hits,misses,txn_commit_count());}
static void *monitor_main(void *unused){(void)unused;while(!stop_monitor){sleep(2);if(stop_monitor)break;pthread_mutex_lock(&engine_lock);write_status();pthread_mutex_unlock(&engine_lock);}return NULL;}
static void protocol_error(const char *message){printf("ERR %s\nEND\n",message);fflush(stdout);}
static void handle_session(char *line){int pc,user,minutes,bill;char extra;
 if(sscanf(line,"SESSION START %d %d %c",&pc,&user,&extra)==2){int rc=session_start(pc,user);if(rc==0)printf("OK\nsession started\nEND\n");else if(rc==-1)protocol_error("PC already has an active session");else if(rc==-2)protocol_error("user already has an active session");else if(rc==-3)protocol_error("session capacity reached");else protocol_error("invalid PC or user id");return;}
 if(sscanf(line,"SESSION STOP %d %c",&pc,&extra)==1){if(session_stop(pc,&minutes,&bill)==0)printf("OK\nminutes=%d\nbill=%d\nEND\n",minutes,bill);else protocol_error("no active session for that PC");return;}
 if(starts_with_ci(line,"SESSION LIST")){if(strlen(line)>12&&line[12]!='\n'&&line[12]!='\r'&&line[12]!='\0'){protocol_error("syntax: SESSION LIST");return;}printf("OK\n");session_print_all(stdout);printf("END\n");return;}
 protocol_error("syntax: SESSION START pc_id user_id | SESSION STOP pc_id | SESSION LIST");}

int main(int argc,char **argv){const char*db_path=argc>1?argv[1]:"cyberdb.db";const char*status_env=getenv("CYBERDB_STATUS");if(status_env&&*status_env)snprintf(status_path,sizeof status_path,"%s",status_env);const char*rate_env=getenv("CYBERDB_RATE");if(rate_env&&atoi(rate_env)>0)session_set_rate(atoi(rate_env));const char*verbose=getenv("CYBERDB_VERBOSE");if(verbose&&atoi(verbose)!=0)buffer_set_verbose(1);
 if(engine_open(db_path)!=0){fprintf(stderr,"[engine] failed to open database: %s\n",db_path);return 1;}const char*sessions_path=getenv("CYBERDB_SESSIONS");if(sessions_init(sessions_path&&*sessions_path?sessions_path:"sessions.dat")<0){fprintf(stderr,"[engine] failed to load sessions\n");engine_close();return 1;}started_at=time(NULL);fprintf(stderr,"[engine] database=%s status=%s rate=%d/min\n",db_path,status_path,rate_env&&atoi(rate_env)>0?atoi(rate_env):2);
 pthread_t monitor;int monitor_started=(pthread_create(&monitor,NULL,monitor_main,NULL)==0);if(!monitor_started)fprintf(stderr,"[engine] warning: status monitor thread unavailable\n");pthread_mutex_lock(&engine_lock);write_status();pthread_mutex_unlock(&engine_lock);
 char line[4096];while(fgets(line,sizeof line,stdin)){size_t len=strlen(line);while(len&& (line[len-1]=='\n'||line[len-1]=='\r'))line[--len]='\0';if(!len)continue;if(starts_with_ci(line,"EXIT")||starts_with_ci(line,"QUIT")){if(line[4]&&line[4]!=' ') {pthread_mutex_lock(&engine_lock);protocol_error("unknown command");pthread_mutex_unlock(&engine_lock);continue;}printf("OK\nbye\nEND\n");fflush(stdout);break;}
 pthread_mutex_lock(&engine_lock);
 if(starts_with_ci(line,"TOKENS ")){char tok[MAX_TOKENS][TOKEN_LEN];int n=tokenize_sql(line+7,tok,MAX_TOKENS);if(n<0)protocol_error("tokenization failed");else{printf("OK\n");for(int i=0;i<n;i++)printf("%d|%s\n",i+1,tok[i]);printf("END\n");}}
 else if(starts_with_ci(line,"SESSION "))handle_session(line);
 else if(starts_with_ci(line,"WALDUMP")){if(line[7]&&line[7]!=' '&&line[7]!='\t')protocol_error("syntax: WALDUMP");else{printf("OK\n");txn_dump_log(stdout);printf("END\n");}}
 else if(starts_with_ci(line,"STATUS")){if(line[6]&&line[6]!=' '&&line[6]!='\t')protocol_error("syntax: STATUS");else{write_status();long h=0,m=0;buffer_stats(&h,&m);printf("OK\nactive_sessions=%d\nbuffer_hits=%ld\nbuffer_misses=%ld\ntxn_commits=%ld\nEND\n",session_count(),h,m,txn_commit_count());}}
 else{char *out=malloc(1<<20);if(!out)protocol_error("out of memory");else{execute_sql(line,out,1<<20);fputs(out,stdout);fflush(stdout);free(out);}}
 pthread_mutex_unlock(&engine_lock);fflush(stdout);
 }
 stop_monitor=1;if(monitor_started)pthread_join(monitor,NULL);pthread_mutex_lock(&engine_lock);write_status();engine_close();pthread_mutex_unlock(&engine_lock);pthread_mutex_destroy(&engine_lock);return 0;}

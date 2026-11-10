/* tell_v11.c — TELL v11: многопоточность (2 потока) + KEY + SLEEP
   gcc -O2 -Wall -Wextra -Wno-misleading-indentation tell_v11.c -o tell -lm -pthread
*/
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <setjmp.h>
struct I; // предварительное объявление, если нужно
static void* run_thread(void* arg);
#ifdef _WIN32
#include <windows.h>
#include <process.h>
typedef HANDLE thread_t;
#define THREAD_CREATE(t, f, a) do { *(t) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(f), (a), 0, NULL); } while(0)
#define THREAD_JOIN(t) WaitForSingleObject((t), INFINITE)
#else
#include <pthread.h>
typedef pthread_t thread_t;
#define THREAD_CREATE(t, f, a) pthread_create((t), NULL, (f), (a))
#define THREAD_JOIN(t) pthread_join((t), NULL)
#endif

#define TAPE_DEF (64UL*1024UL)
#define VL      5000
#define MLL     64
#define MLM     (MLL+2)
#define NV      2048
#define NS      8192
#define NL      2048
#define MLEN    1000000
#define MLINE   80000
#define SSZ     1024
#define MS      10000000UL
#define MD      64
#define NSUB    1024
#define NLAB    4096

/* ---------- panic ---------- */
static void sh(int s){_exit(128+s);}
static void isig(void){
 signal(SIGSEGV,sh);
 signal(SIGFPE,sh);
 signal(SIGABRT,sh);
#ifdef SIGBUS
 signal(SIGBUS,sh);
#endif
#ifdef SIGILL
 signal(SIGILL,sh);
#endif
 signal(SIGINT,sh);
 signal(SIGTERM,sh);
}
static void pan(const char*m){fprintf(stderr,"[PANIC] %s\n",m);exit(1);}
static void*sm(size_t n){void*p=malloc(n?n:1);if(!p)pan("oom");return p;}
static long cv(long v){return v>VL?VL:(v<-VL?-VL:v);}

/* ---------- types ---------- */
typedef struct{char*s;int len;}Str;
typedef struct{long*n;int*s;int len,cap,alive;}List;
typedef struct EF{jmp_buf jb;struct EF*prev;volatile long code;char msg[128];}EF;
typedef struct{char n[MLL];int l;}Lab;
typedef struct{char n[MLL];int l;}Hol;

/* ---------- Interp (один на поток) ---------- */
typedef struct I{
 long r[16];
 unsigned char*tape;size_t tsz;long ptr;
 long stack[SSZ];int sp;
 char vn[NV][MLL];long vv[NV];int nv;
 Str str[NS];int nstr;
 List lst[NL];int nlst;
 EF*err;
 Lab labs[NLAB];int nlab;
 Hol hols[NLAB];int nhol;
 struct{char*buf;size_t len;char lang[16];}sub[NSUB];int nsub;
}I;

static void iinit(I*I,size_t sz){
 memset(I,0,sizeof(*I));I->tsz=sz;
 I->tape=sm(sz);memset(I->tape,0,sz);
}
static void ifree(I*I){
 if(!I)return;
 free(I->tape);
 for(int i=0;i<I->nstr;i++)free(I->str[i].s);
 for(int i=0;i<I->nlst;i++){free(I->lst[i].n);free(I->lst[i].s);}
 for(int i=0;i<I->nsub;i++)free(I->sub[i].buf);
}
static int tget(I*I,long i){
 unsigned long u=(i<0)?(unsigned long)(-i):(unsigned long)i;
 return I->tape[u % I->tsz];
}
static void tset(I*I,long i,int b){
 unsigned long u=(i<0)?(unsigned long)(-i):(unsigned long)i;
 I->tape[u % I->tsz]=(unsigned char)b;
}

/* ---------- vars ---------- */
static int vf(I*I,const char*n){
 for(int i=0;i<I->nv;i++) if(!strcmp(I->vn[i],n)) return i;
 return -1;
}
static int vfc(I*I,const char*n){
 int i=vf(I,n);
 if(i>=0) return i;
 if(I->nv>=NV) pan("vars");
 i=I->nv++;
 memcpy(I->vn[i],n,MLL-1);
 I->vn[i][MLL-1]=0;
 I->vv[i]=0;
 return i;
}
static long vg(I*I,const char*n){int i=vf(I,n);return i<0?0:I->vv[i];}
static void vs(I*I,const char*n,long x){int i=vfc(I,n);I->vv[i]=cv(x);}

/* ---------- strings ---------- */
static int sn(I*I){
 if(I->nstr>=NS) pan("strs");
 int i=I->nstr++;
 I->str[i].s=NULL;
 I->str[i].len=0;
 return i;
}
static int snf(I*I,const char*p,int n){
 int i=sn(I);
 if(n<0)n=0;
 if(n>MLEN)n=MLEN;
 I->str[i].s=sm((size_t)n+1);
 if(n&&p)memcpy(I->str[i].s,p,(size_t)n);
 I->str[i].s[n]=0;
 I->str[i].len=n;
 return i;
}
static Str*sg(I*I,int i){if(i<0||i>=I->nstr)pan("sid");return &I->str[i];}
static void sf(I*I,int i){
 if(i<0||i>=I->nstr)return;
 free(I->str[i].s);
 I->str[i].s=NULL;
 I->str[i].len=0;
}

/* ---------- lists ---------- */
static int ln(I*I){
 if(I->nlst>=NL) pan("lists");
 int i=I->nlst++;
 List*L=&I->lst[i];
 L->n=NULL;L->s=NULL;L->len=L->cap=0;L->alive=1;
 return i;
}
static void lrs(List*L,int need){
 if(need<=L->cap)return;
 int nc=L->cap?L->cap*2:8;
 if(nc<need)nc=need;
 if(nc>MLEN)nc=MLEN;
 L->n=realloc(L->n,sizeof(long)*(size_t)nc);
 L->s=realloc(L->s,sizeof(int)*(size_t)nc);
 if(!L->n||!L->s)pan("list");
 L->cap=nc;
}
static void lpn(List*L,long x){lrs(L,L->len+1);L->n[L->len]=x;L->s[L->len]=-1;L->len++;}
static void lps(List*L,int s){lrs(L,L->len+1);L->n[L->len]=0;L->s[L->len]=s;L->len++;}

/* ---------- exceptions ---------- */
static void thr(I*I,long c,const char*m){
 if(I->err){
  I->err->code=c;
  strncpy(I->err->msg,m?m:"err",127);
  I->err->msg[127]=0;
  longjmp(I->err->jb,1);
 }
 fprintf(stderr,"[UNCAUGHT] %ld: %s\n",c,m?m:"err");
 exit(2);
}
static void tpu(I*I,EF*e){e->prev=I->err;e->msg[0]=0;e->code=0;I->err=e;}
static void tpo(I*I){if(I->err)I->err=I->err->prev;}

/* ---------- labels ---------- */
static void la(I*I,const char*n,int l){
 if(I->nlab>=NLAB)pan("labels");
 memcpy(I->labs[I->nlab].n,n,MLL-1);
 I->labs[I->nlab].n[MLL-1]=0;
 I->labs[I->nlab].l=l;
 I->nlab++;
}
static int lf(I*I,const char*n){
 for(int i=I->nlab-1;i>=0;i--)
  if(!strcmp(I->labs[i].n,n)) return I->labs[i].l;
 return -1;
}
static void ha(I*I,const char*n,int l){
 if(I->nhol>=NLAB)pan("hols");
 memcpy(I->hols[I->nhol].n,n,MLL-1);
 I->hols[I->nhol].n[MLL-1]=0;
 I->hols[I->nhol].l=l;
 I->nhol++;
}
static int hf(I*I,const char*n){
 for(int i=I->nhol-1;i>=0;i--)
  if(!strcmp(I->hols[i].n,n)) return I->hols[i].l;
 return -1;
}

/* ---------- vo (106 опкодов) ---------- */
static void vo(I*I,int op,long a){
 long A=I->r[0],B=I->r[1],C=I->r[2],D=I->r[3];
 switch(op){
  case 1:A=cv(A+1);break;
  case 2:A=cv(A-1);break;
  case 3:putchar((int)(A&255));break;
  case 4:{int c=getchar();A=c==EOF?0:c;}break;
  case 5:if(I->ptr>0)I->ptr--;break;
  case 6:if(I->ptr<(long)I->tsz-1)I->ptr++;break;
  case 7:tset(I,I->ptr,(int)(A&255));break;
  case 8:A=tget(I,I->ptr);break;
  case 9:A=cv(A+B);break;
  case 10:A=cv(A-B);break;
  case 11:A=cv(A*B);break;
  case 12:if(!B)thr(I,1,"div0");A=cv(A/B);break;
  case 13:if(!B)thr(I,2,"mod0");A=cv(A%B);break;
  case 14:A=cv(A^B);break;
  case 15:A=cv(A&B);break;
  case 16:A=cv(A|B);break;
  case 17:A=cv(A<<(B&7));break;
  case 18:A=cv(A>>(B&7));break;
  case 19:{unsigned char b=(unsigned char)(A&255);A=cv(((b<<1)|(b>>7))&255);}break;
  case 20:A=cv(~A);break;
  case 21:A=0;break;
  case 22:A=1;break;
  case 23:B=A;break;
  case 24:A=B;break;
  case 25:{long t=A;A=B;B=t;}break;
  case 26:if(I->ptr<(long)I->tsz-1){tset(I,I->ptr+1,tget(I,I->ptr));I->ptr++;}break;
  case 27:if(I->ptr>0)I->ptr--;break;
  case 28:if(I->sp<SSZ)I->stack[I->sp++]=A;break;
  case 29:if(I->sp>0)A=I->stack[--I->sp];break;
  case 30:I->ptr=a;break;
  case 31:if(!A)I->ptr=a;break;
  case 32:if(A)I->ptr=a;break;
  case 33:if(A<B)I->ptr=a;break;
  case 34:if(A>B)I->ptr=a;break;
  case 35:if(A==B)I->ptr=a;break;
  case 36:if(A!=B)I->ptr=a;break;
  case 37:{long t=I->ptr;I->ptr=a;if(I->sp<SSZ)I->stack[I->sp++]=t;}break;
  case 38:if(I->sp>0)I->ptr=I->stack[--I->sp];break;
  case 39:I->r[0]=A;I->r[1]=B;I->r[2]=C;I->r[3]=D;exit(0);
  case 40:printf("%ld\n",A);break;
  case 41:{long x;if(scanf("%ld",&x)==1)A=x;else A=0;}break;
  case 42:A=(long)sqrt((double)(A<0?-A:A));break;
  case 43:A=A<0?-A:A;break;
  case 44:A=-A;break;
  case 45:A=A<B?A:B;break;
  case 46:A=A>B?A:B;break;
  case 47:{long x=1,y=2;while(y<A){long z=x+y;x=y;y=z;}A=x;}break;
  case 48:{int f=0;if(A>=2){f=1;for(long i=2;i*i<=A;i++)if(!(A%i)){f=0;break;}}A=f;}break;
  case 49:{long r=1;for(long i=2;i<=A&&i<20;i++)r*=i;A=cv(r);}break;
  case 50:{long x=A,y=B;while(y){long z=x%y;x=y;y=z;}A=x;}break;
  case 51:{long r=1;for(long i=0;i<(B&15);i++)r*=A;A=cv(r);}break;
  case 52:A=(long)log(A<2?2:(double)A);break;
  case 53:A=(long)(sin((double)A)*100);break;
  case 54:A=(long)(cos((double)A)*100);break;
  case 55:A=(long)(tan((double)A)*100);break;
  case 56:{
   uint32_t c=0xFFFFFFFFU;
   for(int i=0;i<4;i++){
    c^=(uint32_t)(A>>(i*8))&255;
    for(int j=0;j<8;j++)c=(c&1)?(0xEDB88320U^(c>>1)):(c>>1);
   }
   A=c&255;
  }break;
  case 57:{
   uint32_t h=2166136261U;
   for(int i=0;i<4;i++){h^=(uint32_t)(A>>(i*8))&255;h*=16777619U;}
   A=h&255;
  }break;
  case 58:{unsigned long x=(unsigned long)A;x^=x<<13;x^=x>>7;x^=x<<17;A=x&255;}break;
  case 59:A=cv((A*0x9E3779B1L)&255);break;
  case 60:A=0;break;
  case 61:A=0xC0;break;
  case 62:A&=255;break;
  case 63:A=cv(A+B+C);break;
  case 64:A=cv(A*B*C);break;
  case 65:A=cv(A^B^C);break;
  case 66:A=cv(A+B+C+D);break;
  case 67:A=cv(A*B*C*D);break;
  case 68:if(A<0)A=0;if(A>=(long)I->tsz)A=(long)I->tsz-1;I->ptr=A;break;
  case 69:A=I->ptr;break;
  case 70:I->r[A&15]=B;break;
  case 71:A=I->r[A&15];break;
  case 72:A=cv(A<<2);break;
  case 73:A=cv(A>>2);break;
  case 74:A=cv(A<<4);break;
  case 75:A=cv(A>>4);break;
  case 76:A=cv(A+B+C+D+1);break;
  case 77:A=cv(A*2+B);break;
  case 78:A=cv(A+B*2);break;
  case 79:A=cv(A-B+C-D);break;
  case 80:A=cv((A^B)&(C|D));break;
  case 81:A=cv((A+B)>>1);break;
  case 82:A=cv((A*B)>>1);break;
  case 83:A=cv((A<<1)+B);break;
  case 84:A=cv((A>>1)+B);break;
  case 85:A=cv(A+B-C);break;
  case 86:A=cv(A*B-C);break;
  case 87:if(!B)thr(I,1,"div0");A=cv(A/B+C);break;
  case 88:if(!B)thr(I,2,"mod0");A=cv(A%B+C);break;
  case 89:A=cv((A&B)^C);break;
  case 90:A=cv(A|(B&C));break;
  case 91:A=cv((A^B)|C);break;
  case 92:A=cv(~(A&B));break;
  case 93:A=cv(~(A|B));break;
  case 94:A=cv(~(A^B));break;
  case 95:if(I->ptr<(long)I->tsz-1){tset(I,I->ptr+1,(int)(B&255));I->ptr++;}break;
  case 96:B=tget(I,I->ptr);break;
  case 97:if(I->ptr>0){tset(I,I->ptr-1,(int)(A&255));I->ptr--;}break;
  case 98:A=tget(I,I->ptr>0?I->ptr-1:0);break;
  case 99:if(I->sp<SSZ-1){I->stack[I->sp++]=A;I->stack[I->sp++]=B;}break;
  case 100:if(I->sp>1){B=I->stack[--I->sp];A=I->stack[--I->sp];}break;
  case 101:if(I->sp<SSZ)I->stack[I->sp++]=I->ptr;break;
  case 102:if(I->sp>0)I->ptr=I->stack[--I->sp];break;
  case 103:if(I->sp<SSZ)I->stack[I->sp++]=0xC0FFEE;break;
  case 104:if(I->sp<SSZ)I->stack[I->sp++]=0;break;
 }
 I->r[0]=A;I->r[1]=B;I->r[2]=C;I->r[3]=D;
}

/* ---------- sub langs ---------- */
static void skros(const unsigned char*d,long n,I*I){
 for(long i=0;i<n;i++){
  char c=d[i];long A=I->r[0],B=I->r[1];
  switch(c){
   case 'G':A=cv(A+1);break;
   case 'S':A=cv(A-1);break;
   case 'H':putchar((int)(A&255));break;
   case 'T':{int x=getchar();A=x==EOF?0:x;}break;
   case 'L':if(I->ptr>0)I->ptr--;break;
   case 'R':if(I->ptr<(long)I->tsz-1)I->ptr++;break;
   case 'K':tset(I,I->ptr,(int)(A&255));break;
   case 'E':A=tget(I,I->ptr);break;
   case '+':A=cv(A+B);break;
   case '-':A=cv(A-B);break;
   case '*':A=cv(A*B);break;
   case '/':if(!B)thr(I,1,"div0");A=cv(A/B);break;
   case '^':A=cv(A^B);break;
   case '&':A=cv(A&B);break;
   case '|':A=cv(A|B);break;
   case '!':A=cv(~A);break;
   case '0':A=0;break;
   case '1':A=1;break;
   case 'B':B=A;break;
   case 'A':A=B;break;
   case 'W':{long t=A;A=B;B=t;}break;
   case 'P':if(I->sp<SSZ)I->stack[I->sp++]=A;break;
   case 'Q':if(I->sp>0)A=I->stack[--I->sp];break;
   case 'Z':return;
  }
  I->r[0]=A;I->r[1]=B;
 }
}
static void sotg(const unsigned char*d,long n,I*I){
 for(long i=0;i<n;i++){
  char c=d[i];long A=I->r[0],B=I->r[1];
  switch(c){
   case 'O':A=cv(A+1);break;
   case 'T':A=cv(A-1);break;
   case 'G':putchar((int)(A&255));break;
   case 'A':{int x=getchar();A=x==EOF?0:x;}break;
   case '-':A=cv(A-1);break;
   case '+':A=cv(A+1);break;
   case '@':putchar((int)(A&255));break;
   case '*':A=cv(A*B);break;
   case ':':tset(I,I->ptr,(int)(A&255));break;
   case ';':A=tget(I,I->ptr);break;
   case '!':A=cv(~A);break;
   case '?':return;
   case '/':if(!B)thr(I,1,"div0");A=cv(A/B);break;
   case '\\':if(!B)thr(I,2,"mod0");A=cv(A%B);break;
   case 'J':if(I->sp<SSZ)I->stack[I->sp++]=A;break;
   case 'K':if(I->sp>0)A=I->stack[--I->sp];break;
   case 'L':{long t=A;A=B;B=t;}break;
   case 'Q':A=cv(A^B);break;
   case 'W':A=cv(A&B);break;
   case 'E':A=cv(A|B);break;
   case 'R':A=B;break;
   case 'Y':A=cv(A+1);break;
   case 'U':A=cv(A-1);break;
   case 'I':A=cv(A*2);break;
   case 'P':A=cv(A^2);break;
   case 'F':A&=15;break;
   case 'H':A=(~A)&255;break;
   case 'Z':A=0;break;
   case 'X':A=1;break;
   case 'C':A=255;break;
   case 'V':A=tget(I,0);break;
   case 'B':tset(I,0,(int)(A&255));break;
   case 'N':A=cv(A+B);break;
   case 'M':A=cv(A-B);break;
  }
  I->r[0]=A;I->r[1]=B;
 }
}
static void sbabl(const unsigned char*d,long n,I*I){
 for(long ip=n-1;ip>=0;ip--){
  char c=d[ip];
  if(c=='/'||c=='+')I->r[0]=cv(I->r[0]+1);
  else if(c=='\\'||c=='-')I->r[0]=cv(I->r[0]-1);
  else if(c=='*')putchar((int)(I->r[0]&255));
  else if(c=='['){
   if(!I->r[0]){
    int de=1;
    while(ip>0&&de){ip--;if(d[ip]==']')de++;else if(d[ip]=='[')de--;}
   }
  }
  else if(c==']'){
   if(I->r[0]){
    int de=1;
    while(ip<n-1&&de){ip++;if(d[ip]=='[')de++;else if(d[ip]==']')de--;}
   }
  }
 }
}
static int hv(unsigned char c){
 if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 if(c>='A'&&c<='F')return c-'A'+10;
 return -1;
}
static void shex(const unsigned char*d,long n,I*I){
 for(long i=0;i+1<n;){
  if(d[i]==' '||d[i]=='\n'||d[i]=='\r'||d[i]=='\t'){i++;continue;}
  int h=hv(d[i]),l=hv(d[i+1]);
  if(h<0||l<0){i++;continue;}
  unsigned char c=(unsigned char)(h*16+l);i+=2;
  long A=I->r[0],B=I->r[1];
  switch(c){
   case 1:A=cv(A+1);break;
   case 2:A=cv(A-1);break;
   case 3:putchar((int)(A&255));break;
   case 4:{int x=getchar();A=x==EOF?0:x;}break;
   case 5:if(I->ptr>0)I->ptr--;break;
   case 6:if(I->ptr<(long)I->tsz-1)I->ptr++;break;
   case 7:tset(I,I->ptr,(int)(A&255));break;
   case 8:A=tget(I,I->ptr);break;
   case 9:A=cv(A+B);break;
   case 10:A=cv(A-B);break;
   case 11:A=cv(A^B);break;
   case 12:A=cv(A*B);break;
   case 13:if(!B)thr(I,1,"div0");A=cv(A/B);break;
   case 14:if(!B)thr(I,2,"mod0");A=cv(A%B);break;
   case 15:A=cv(~A);break;
   case 16:A=0;break;
   case 17:A=1;break;
   case 18:B=A;break;
   case 19:A=B;break;
   case 26:return;
  }
  I->r[0]=A;I->r[1]=B;
 }
}
static void srun(I*I,const char*lg,const unsigned char*c,long n){
 char L[16];int k=0;
 for(int i=0;lg[i]&&k<15;i++)L[k++]=(char)toupper((unsigned char)lg[i]);
 L[k]=0;
 if(!strcmp(L,"KROS")||!strcmp(L,"KR"))skros(c,n,I);
 else if(!strcmp(L,"OTG"))sotg(c,n,I);
 else if(!strcmp(L,"BABL")||!strcmp(L,"B"))sbabl(c,n,I);
 else if(!strcmp(L,"HEXAGRAM")||!strcmp(L,"HEX")||!strcmp(L,"H"))shex(c,n,I);
}

/* ---------- lexer ---------- */
typedef struct{char**l;int n;}TL;
static void tbu(const unsigned char*d,long n,TL*p){
 p->l=sm(sizeof(char*)*MLINE);p->n=0;
 long i=0;
 while(i<n&&p->n<MLINE){
  long s=i;
  while(i<n&&d[i]!='\n')i++;
  p->l[p->n]=sm((size_t)(i-s)+1);
  memcpy(p->l[p->n],d+s,(size_t)(i-s));
  p->l[p->n][i-s]=0;
  p->n++;
  if(i<n)i++;
 }
}
static void tfr(TL*p){for(int i=0;i<p->n;i++)free(p->l[i]);free(p->l);}

static void SW(const char*d,int n,int*i){
 while(*i<n){
  char c=d[*i];
  if(c==' '||c=='\t'||c=='\r'){(*i)++;continue;}
  if(c==';'||(c=='/'&&*i+1<n&&d[*i+1]=='/')){while(*i<n&&d[*i]!='\n')(*i)++;continue;}
  if(c=='#'&&*i+1<n&&isdigit((unsigned char)d[*i+1]))break;
  if(c=='#'&&(*i+1>=n||!isdigit((unsigned char)d[*i+1]))){while(*i<n&&d[*i]!='\n')(*i)++;continue;}
  break;
 }
}
static int IL(const char*d,int n,int i){
 if(i>=n)return 0;
 char c=d[i];
 if(!(isalpha((unsigned char)c)||c=='_'))return 0;
 int j=i+1;
 while(j<n&&(isalnum((unsigned char)d[j])||d[j]=='_'))j++;
 return j-i;
}
static int NP(const char*d,int n,int*i,long*o){
 SW(d,n,i);
 int j=*i,ng=0;
 if(j<n&&d[j]=='-'){ng=1;j++;}
 if(j>=n||!isdigit((unsigned char)d[j]))return 0;
 long x=0;
 while(j<n&&isdigit((unsigned char)d[j])){x=x*10+(d[j]-'0');j++;}
 *o=ng?-x:x;*i=j;return 1;
}
static int KW(const char*d,int n,int i,const char*s){
 int L=(int)strlen(s);
 if(i+L>n)return 0;
 if(strncmp(d+i,s,L))return 0;
 if(i+L<n&&(isalnum((unsigned char)d[i+L])||d[i+L]=='_'))return 0;
 return 1;
}

/* ---------- expression evaluator ---------- */
static long EX(I*I,const char*d,int n,int*i,int*ok);
static int PREC(char a,char b){
 static const char*op[]={"||","&&","==","!=","<=",">=","<<",">>","<",">","+","-","*","/","%","&","^","|",NULL};
 static const int pr[]={1,2,3,3,4,4,5,5,6,6,7,7,8,8,8,9,10,11};
 for(int k=0;op[k];k++)if(a==op[k][0]&&(op[k][1]==0||b==op[k][1]))return pr[k];
 return -1;
}
static long PR(I*I,const char*d,int n,int*i,int*ok){
 SW(d,n,i);
 if(*i>=n){*ok=0;return 0;}
 char c=d[*i];
 if(c=='('){
  (*i)++;
  long x=EX(I,d,n,i,ok);
  SW(d,n,i);
  if(*i<n&&d[*i]==')')(*i)++;
  else *ok=0;
  return x;
 }
 if(c=='-'||isdigit((unsigned char)c)){long x;if(NP(d,n,i,&x))return x;*ok=0;return 0;}
 if(c=='~'){(*i)++;return ~PR(I,d,n,i,ok);}
 if(c=='!'){(*i)++;return !PR(I,d,n,i,ok);}
 if(c=='"'){
  (*i)++;
  long len=0;
  while(*i<n&&d[*i]!='"'){if(d[*i]=='\\'&&*i+1<n)*i+=2;else(*i)++;len++;}
  if(*i<n)(*i)++;
  return len;
 }
 int L=IL(d,n,*i);
 if(L>0){
  char nm[MLL];int l=L<MLL-1?L:MLL-1;
  memcpy(nm,d+*i,(size_t)l);nm[l]=0;*i+=L;
  if(!strcmp(nm,"A"))return I->r[0];
  if(!strcmp(nm,"B"))return I->r[1];
  if(!strcmp(nm,"C"))return I->r[2];
  if(!strcmp(nm,"D"))return I->r[3];
  if(!strcmp(nm,"P"))return I->ptr;
  return vg(I,nm);
 }
 *ok=0;return 0;
}
static long EX(I*I,const char*d,int n,int*i,int*ok){
 long lhs=PR(I,d,n,i,ok);
 if(!*ok)return 0;
 for(;;){
  SW(d,n,i);
  if(*i>=n)break;
  char c=d[*i];char c2=*i+1<n?d[*i+1]:0;
  int p=PREC(c,c2);
  if(p<0)break;
  int two=(p==3||p==4||p==5);
  char op[3]={c,two?c2:0,0};
  (*i)+=two?2:1;
  long rhs=PR(I,d,n,i,ok);
  if(!*ok)return 0;
  if(!strcmp(op,"+"))lhs=cv(lhs+rhs);
  else if(!strcmp(op,"-"))lhs=cv(lhs-rhs);
  else if(!strcmp(op,"*"))lhs=cv(lhs*rhs);
  else if(!strcmp(op,"/")){if(!rhs)thr(I,1,"div0");lhs=cv(lhs/rhs);}
  else if(!strcmp(op,"%")){if(!rhs)thr(I,2,"mod0");lhs=cv(lhs%rhs);}
  else if(!strcmp(op,"<<"))lhs=cv(lhs<<(rhs&7));
  else if(!strcmp(op,">>"))lhs=cv(lhs>>(rhs&7));
  else if(!strcmp(op,"&"))lhs=cv(lhs&rhs);
  else if(!strcmp(op,"|"))lhs=cv(lhs|rhs);
  else if(!strcmp(op,"^"))lhs=cv(lhs^rhs);
  else if(!strcmp(op,"=="))lhs=lhs==rhs;
  else if(!strcmp(op,"!="))lhs=lhs!=rhs;
  else if(!strcmp(op,"<"))lhs=lhs<rhs;
  else if(!strcmp(op,">"))lhs=lhs>rhs;
  else if(!strcmp(op,"<="))lhs=lhs<=rhs;
  else if(!strcmp(op,">="))lhs=lhs>=rhs;
  else if(!strcmp(op,"&&"))lhs=lhs&&rhs;
  else if(!strcmp(op,"||"))lhs=lhs||rhs;
 }
 return lhs;
}

static void AS(I*I,const char*nm,long x){
 if(!strcmp(nm,"A")){I->r[0]=cv(x);return;}
 if(!strcmp(nm,"B")){I->r[1]=cv(x);return;}
 if(!strcmp(nm,"C")){I->r[2]=cv(x);return;}
 if(!strcmp(nm,"D")){I->r[3]=cv(x);return;}
 if(!strcmp(nm,"P")){
  if(x<0)x=0;
  if((size_t)x>I->tsz-1)x=(long)I->tsz-1;
  I->ptr=x;
  return;
 }
 vs(I,nm,x);
}

/* ---------- block scanning ---------- */
static int MT(const char*d,int n,int s){
 int j=s,depth=1;
 while(j<n&&depth>0){
  char c=d[j];
  if(c=='"'||c=='\''){
   char q=c;j++;
   while(j<n&&d[j]!=q){if(d[j]=='\\'&&j+1<n)j+=2;else j++;}
   if(j<n)j++;
   continue;
  }
  if(c=='<'&&j+1<n&&d[j+1]=='<'){j+=2;continue;}
  if(c=='>'&&j+1<n&&d[j+1]=='>'){j+=2;continue;}
  if(c=='<')depth++;
  else if(c=='>')depth--;
  j++;
 }
 return depth==0?j-1:-1;
}
static int FC(const char*d,int n,int i){while(i<n&&d[i]!=':')i++;return i<n?i:-1;}
static int FE(TL*p,int L0){
 int depth=1;
 for(int L=L0+1;L<p->n;L++){
  const char*dd=p->l[L];int nn=(int)strlen(dd);int q=0;
  SW(dd,nn,&q);
  if(q>=nn)continue;
  if(KW(dd,nn,q,"WHILE")||KW(dd,nn,q,"IF")||KW(dd,nn,q,"TRY")||
     KW(dd,nn,q,"FOREACH")||KW(dd,nn,q,"SUB")||KW(dd,nn,q,"THREAD"))depth++;
  if(KW(dd,nn,q,"END")){if(--depth==0)return L;}
 }
 return -1;
}
static int FC2(TL*p,int L0,int eL,const char*kw){
 int depth=0;
 for(int K=L0+1;K<eL;K++){
  const char*dd=p->l[K];int nn=(int)strlen(dd);int q=0;
  SW(dd,nn,&q);
  if(q>=nn)continue;
  if(KW(dd,nn,q,"WHILE")||KW(dd,nn,q,"IF")||KW(dd,nn,q,"TRY")||
     KW(dd,nn,q,"FOREACH")||KW(dd,nn,q,"SUB")||KW(dd,nn,q,"THREAD"))depth++;
  else if(KW(dd,nn,q,"END"))depth--;
  else if(depth==0&&KW(dd,nn,q,kw))return K;
 }
 return -1;
}

/* ---------- HOL_JMP parse ---------- */
static void hparse(I*I,const char*d,int n){
 int i=0;
 while(i<n){
  SW(d,n,&i);
  if(i>=n)break;
  while(i<n&&d[i]!='(')i++;
  if(i>=n)break;
  i++;
  int ls=i;
  while(i<n&&d[i]!=')')i++;
  if(i>=n)break;
  long line=0;int h=0;
  for(int k=ls;k<i;k++){
   if(isdigit((unsigned char)d[k])){line=line*10+(d[k]-'0');h=1;}
   else if(!isspace((unsigned char)d[k])){h=0;break;}
  }
  if(!h)return;
  i++;
  while(i<n&&(d[i]==' '||d[i]=='\t'))i++;
  if(i<n&&d[i]==':')i++;
  while(i<n&&(d[i]==' '||d[i]=='\t'))i++;
  if(i>=n||d[i]!='(')return;
  i++;
  int ns=i;
  while(i<n&&d[i]!=')')i++;
  if(i>=n)return;
  int ne=i;
  char nm[MLL];int le=ne-ns;if(le>MLL-1)le=MLL-1;
  memcpy(nm,d+ns,(size_t)le);nm[le]=0;
  if(line>=1)ha(I,nm,(int)(line-1));
  if(i<n)i++;
  while(i<n&&(d[i]==' '||d[i]=='\t'))i++;
  if(i<n&&d[i]==';'){i++;continue;}
  if(i>=n)break;
 }
}

/* ---------- print ---------- */
static void prs(I*I,int sid){
 Str*s=sg(I,sid);
 if(s->s)fwrite(s->s,1,(size_t)s->len,stdout);
}
static void DOP(I*I,const char*d,int n,int*ip){
 int i=*ip;SW(d,n,&i);
 while(i<n){
  SW(d,n,&i);
  if(i>=n)break;
  char c=d[i];
  if(c=='"'||c=='\''){
   char q=c;i++;
   while(i<n&&d[i]!=q){
    if(d[i]=='\\'&&i+1<n){
     char e=d[i+1];i+=2;
     putchar(e=='n'?'\n':e=='t'?'\t':e=='r'?'\r':e=='0'?0:e);
    }
    else{putchar((unsigned char)d[i]);i++;}
   }
   if(i<n)i++;
  }
  else if(c=='$'||c=='&'){
   i++;
   int L=IL(d,n,i);
   if(L>0&&L<MLL){
    char nm[MLL];memcpy(nm,d+i,(size_t)L);nm[L]=0;i+=L;
    if(nm[0]=='L'&&nm[1]=='_'&&nm[2]){
     long lid=vg(I,nm+2);
     if(lid>=0&&lid<I->nlst&&I->lst[lid].alive){
      List*Lt=&I->lst[lid];
      putchar('[');
      for(int k=0;k<Lt->len;k++){
       if(k)printf(", ");
       if(Lt->s[k]>=0){putchar('"');prs(I,Lt->s[k]);putchar('"');}
       else printf("%ld",Lt->n[k]);
      }
      putchar(']');
      goto nxt;
     }
    }
    {
     char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
     long sid=vg(I,snm);
     if(sid>=0&&sid<I->nstr)prs(I,(int)sid);
     else printf("%ld",vg(I,nm));
    }
    nxt:;
   }
  }
  else{
   int ok=1;
   long x=EX(I,d,n,&i,&ok);
   if(!ok){fprintf(stderr,"[PRINT] err\n");*ip=i;return;}
   printf("%ld",x);
  }
  SW(d,n,&i);
  if(i<n&&d[i]==','){i++;continue;}
  break;
 }
 putchar('\n');
 *ip=i;
}

/* ---------- string literal ---------- */
static int SL(I*I,const char*d,int n,int i,int*end){
 if(i>=n||d[i]!='"')return -1;
 int j=i+1,len=0;
 while(j<n&&d[j]!='"'){
  if(d[j]=='\\'&&j+1<n)j+=2;else j++;
  len++;
 }
 if(j>=n)return -1;
 char*buf=sm((size_t)len+1);
 int k=0,src=i+1;
 while(src<j){
  if(d[src]=='\\'&&src+1<j){
   char e=d[src+1];src+=2;
   buf[k++]=e=='n'?'\n':e=='t'?'\t':e=='r'?'\r':e=='0'?0:e;
  }
  else buf[k++]=d[src++];
 }
 int id=snf(I,buf,k);
 free(buf);
 *end=j+1;
 return id;
}

/* ---------- TB ---------- */
static void TB(I*I,TL*p,volatile int L0,int lt,int dp){
 if(dp>MD)thr(I,99,"depth");
 int L=L0;
 while(L<=lt&&L<p->n){
  const char*d=p->l[L];int n=(int)strlen(d);int i=0;
  SW(d,n,&i);
  if(i>=n){L++;continue;}

  int hc=0;long cvl=0;
  if(d[i]=='<'&&!(i+1<n&&d[i+1]=='<')){
   int j=MT(d,n,i+1);
   if(j>0){
    int ok=1,kk=i+1;
    cvl=EX(I,d,j,&kk,&ok);
    if(!ok)cvl=0;
    hc=1;
    i=j+1;
    SW(d,n,&i);
    if(i>=n){
     if(cvl)I->r[0]=cvl;
     L++;continue;
    }
    if(!cvl){L++;continue;}
   }
  }

  if(d[i]=='#'){
   int j=i+1;long N=0;int h=0;
   while(j<n&&isdigit((unsigned char)d[j])){N=N*10+(d[j]-'0');j++;h=1;}
   if(h&&N>=0&&N<p->n){L=(int)N;continue;}
   L++;continue;
  }

  if(KW(d,n,i,"LABEL")){
   int j=i+5;SW(d,n,&j);
   int ll=IL(d,n,j);
   if(ll>0&&ll<MLL){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll);nm[ll]=0;
    la(I,nm,L);
   }
   L++;continue;
  }
  if(KW(d,n,i,"dow")){
   int j=i+3;SW(d,n,&j);
   if(j<n&&d[j]=='('){
    j++;int js=j;
    while(j<n&&d[j]!=')')j++;
    if(j<n){
     char nm[MLL];int ll=j-js;if(ll>MLL-1)ll=MLL-1;
     memcpy(nm,d+js,(size_t)ll);nm[ll]=0;
     la(I,nm,L);
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"HOL_JMP")){
   int j=i+7;SW(d,n,&j);
   if(j<n&&d[j]==':')j++;
   int k=j;SW(d,n,&k);
   if(k<n&&d[k]=='('){hparse(I,d+j,n-j);L++;continue;}
   int K=L+1;
   while(K<p->n){
    const char*dd=p->l[K];int nn=(int)strlen(dd);int q=0;
    SW(dd,nn,&q);
    if(q>=nn){K++;continue;}
    if(isdigit((unsigned char)dd[q])){
     int q2=q;
     while(q2<nn&&isdigit((unsigned char)dd[q2]))q2++;
     if(q2<nn&&dd[q2]=='_'){hparse(I,dd,nn);K++;continue;}
    }
    break;
   }
   L=K;continue;
  }

  if(i+4<n&&!strncmp(d+i,"hol_",4)&&(isalpha((unsigned char)d[i+4])||d[i+4]=='_')){
   int j=i+4;int ll=IL(d,n,j);
   if(ll>0&&ll<MLL){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll);nm[ll]=0;
    int t=hf(I,nm);
    if(t>=0){L=t;continue;}
    fprintf(stderr,"[hol] '%s' not declared\n",nm);
   }
   L++;continue;
  }

  if(KW(d,n,i,"GOTO")||KW(d,n,i,"JMP")){
   int j=i+(d[i]=='G'?4:3);SW(d,n,&j);
   char nm[MLL]={0};
   if(j<n&&(d[j]=='"'||d[j]=='\'')){
    char q=d[j++];int js=j;
    while(j<n&&d[j]!=q)j++;
    int le=j-js;if(le>MLL-1)le=MLL-1;
    memcpy(nm,d+js,(size_t)le);nm[le]=0;
   }
   else{
    int ll=IL(d,n,j);
    if(ll>0&&ll<MLL){memcpy(nm,d+j,(size_t)ll);nm[ll]=0;}
   }
   int t=lf(I,nm);
   if(t<0)thr(I,11,"label");
   L=t;continue;
  }

  if(KW(d,n,i,"JHP")){
   int j=i+3;SW(d,n,&j);
   char nm[MLL]={0};
   if(j<n&&(d[j]=='"'||d[j]=='\'')){
    char q=d[j++];int js=j;
    while(j<n&&d[j]!=q)j++;
    int le=j-js;if(le>MLL-1)le=MLL-1;
    memcpy(nm,d+js,(size_t)le);nm[le]=0;
   }
   else{
    int ll=IL(d,n,j);
    if(ll>0&&ll<MLL){memcpy(nm,d+j,(size_t)ll);nm[ll]=0;}
   }
   int dj=hc?1:(I->r[0]!=0);
   if(dj){
    int t=lf(I,nm);
    if(t<0)thr(I,11,"jhp");
    L=t;continue;
   }
   L++;continue;
  }

  if(KW(d,n,i,"CND")){
   int j=i+3;SW(d,n,&j);
   if(j<n&&d[j]=='<'){
    int k2=MT(d,n,j+1);
    if(k2>0){
     int ok=1,kk=j+1;
     long x=EX(I,d,k2,&kk,&ok);
     if(ok)I->r[0]=x;
    }
   }
   L++;continue;
  }

  /* --- НОВЫЕ КОМАНДЫ KEY / SLEEP --- */
  if(KW(d,n,i,"KEY")){
      int c=getchar();
      if(c==EOF)c=0;
      I->r[0]=c;
      L++;continue;
  }
  if(KW(d,n,i,"SLEEP")){
      int j=i+5;SW(d,n,&j);
      long a;
      if(NP(d,n,&j,&a)){
          #ifdef _WIN32
          Sleep((DWORD)a);
          #else
          usleep(a * 1000);
          #endif
      }
      L++;continue;
  }
  /* ----------------------------- */

  /* --- МНОГОПОТОЧНОСТЬ (THREAD) --- */
  if(KW(d,n,i,"THREAD")){
      int eL=FE(p,L);
      if(eL<0)thr(I,99,"missing END for THREAD");
      
      // Извлекаем имя потока (например, THREAD A:)
      int j=i+6;SW(d,n,&j);
      int ll=IL(d,n,j);
      char tname[MLL]={0};
      if(ll>0&&ll<MLL){ memcpy(tname,d+j,(size_t)ll); tname[ll]=0; }

      // Создаем текст для нового потока
      size_t total=0;
      for(int K=L+1;K<eL;K++) total+=strlen(p->l[K])+1;
      char*code=sm(total+1);
      size_t off=0;
      for(int K=L+1;K<eL;K++){
          size_t l=strlen(p->l[K]);
          memcpy(code+off,p->l[K],l); off+=l; code[off++]='\n';
      }
      code[off]=0;

      // Запускаем поток
      #ifdef _WIN32
      // Для Windows используем CreateThread и ожидаем завершения
      HANDLE th = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)run_thread, (void*)code, 0, NULL);
      WaitForSingleObject(th, INFINITE);
      CloseHandle(th);
      #else
      // Для Linux/macOS используем pthread
      // (код run_thread должен быть объявлен выше, см. ниже)
      pthread_t th;
      pthread_create(&th, NULL, run_thread, (void*)code);
      pthread_join(th, NULL);
      #endif
      
      free(code);
      L=eL+1;
      continue;
  }

  if(KW(d,n,i,"IMPORT")){
   int j=i+6;SW(d,n,&j);
   int e2;int sid=SL(I,d,n,j,&e2);
   if(sid>=0){
    Str*s=sg(I,sid);
    FILE*f=fopen(s->s,"rb");
    if(!f){sf(I,sid);thr(I,10,"import");}
    else{
     fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
     if(sz>0){
      unsigned char*buf=sm((size_t)sz);
      if(fread(buf,1,(size_t)sz,f)==(size_t)sz){
       TL sb;memset(&sb,0,sizeof(sb));tbu(buf,sz,&sb);
       for(int L2=0;L2<sb.n;L2++){
        const char*dd=sb.l[L2];int nn=(int)strlen(dd);int q=0;
        SW(dd,nn,&q);
        if(q>=nn)continue;
        if(KW(dd,nn,q,"LABEL")){
         int j2=q+5;SW(dd,nn,&j2);
         int ll=IL(dd,nn,j2);
         if(ll>0&&ll<MLL){
          char nm[MLL];memcpy(nm,dd+j2,(size_t)ll);nm[ll]=0;
          la(I,nm,L2);
         }
        }
        else if(KW(dd,nn,q,"dow")){
         int j2=q+3;SW(dd,nn,&j2);
         if(j2<nn&&dd[j2]=='('){
          j2++;int js=j2;
          while(j2<nn&&dd[j2]!=')')j2++;
          if(j2<nn){
           char nm[MLL];int ll=j2-js;if(ll>MLL-1)ll=MLL-1;
           memcpy(nm,dd+js,(size_t)ll);nm[ll]=0;
           la(I,nm,L2);
          }
         }
        }
        else if(KW(dd,nn,q,"HOL_JMP")){
         int j2=q+7;SW(dd,nn,&j2);
         if(j2<nn&&dd[j2]==':')j2++;
         int k2=j2;SW(dd,nn,&k2);
         if(k2<nn&&dd[k2]=='(')hparse(I,dd+j2,nn-j2);
        }
       }
       TB(I,&sb,0,sb.n-1,dp+1);
       tfr(&sb);
      }
      free(buf);
     }
     fclose(f);
     sf(I,sid);
    }
   }
   L++;continue;
  }

  if(KW(d,n,i,"SUB")){
   int j=i+3;SW(d,n,&j);
   if(j<n&&d[j]=='('){
    j++;int js=j;
    while(j<n&&d[j]!=')')j++;
    if(j<n){
     char lg[16];int ll=j-js;if(ll>15)ll=15;
     memcpy(lg,d+js,(size_t)ll);lg[ll]=0;j++;
     char bf[16384];int bl=0;
     for(int q=j;q<n&&bl<16383;q++)bf[bl++]=(char)d[q];
     bf[bl++]='\n';
     int eL=-1;
     for(int K=L+1;K<p->n&&bl<16383;K++){
      const char*dd=p->l[K];int nn=(int)strlen(dd);int q=0;
      SW(dd,nn,&q);
      if(KW(dd,nn,q,"END")){eL=K;break;}
      for(int m=0;m<nn&&bl<16383;m++)bf[bl++]=(char)dd[m];
      bf[bl++]='\n';
     }
     if(eL>=0){
      bf[bl]=0;
      long ga=I->r[0];
      srun(I,lg,(unsigned char*)bf,bl);
      I->r[0]=ga;
      L=eL+1;continue;
     }
    }
   }
   L++;continue;
  }

  if(KW(d,n,i,"TRY")){
   volatile int eL=FE(p,L);
   if(eL<0)thr(I,99,"try end");
   volatile int cL=FC2(p,L,eL,"CATCH");
   volatile int fL=FC2(p,L,eL,"FINALLY");
   volatile int te=(cL>=0)?cL-1:((fL>=0)?fL-1:eL-1);
   volatile int ce=(fL>=0)?fL-1:eL-1;
   EF ef;tpu(I,&ef);
   if(setjmp(ef.jb)==0){
    TB(I,p,L+1,te,dp+1);
    tpo(I);
   } else {
    long ec=ef.code;
    tpo(I);
    if(cL>=0){
     const char*cd=p->l[cL];int cn=(int)strlen(cd);int q=5;
     SW(cd,cn,&q);
     int ll=IL(cd,cn,q);
     if(ll>0){
      char nm[MLL];memcpy(nm,cd+q,(size_t)ll);nm[ll]=0;
      AS(I,nm,ec);
     }
     EF e2;tpu(I,&e2);
     if(setjmp(e2.jb)==0){
      TB(I,p,cL+1,ce,dp+1);
      tpo(I);
     } else {
      tpo(I);
      if(fL>=0)TB(I,p,fL+1,eL-1,dp+1);
      thr(I,e2.code,e2.msg);
     }
    } else {
     if(fL>=0)TB(I,p,fL+1,eL-1,dp+1);
     thr(I,ec,ef.msg);
    }
   }
   if(fL>=0)TB(I,p,fL+1,eL-1,dp+1);
   L=eL+1;continue;
  }
  if(KW(d,n,i,"THROW")){
   int j=i+5;SW(d,n,&j);
   int ok=1;
   long x=EX(I,d,n,&j,&ok);
   thr(I,ok?x:0,"thrown");
  }

  int iw=KW(d,n,i,"WHILE"),ii=KW(d,n,i,"IF"),ife=KW(d,n,i,"FOREACH");
  if(iw||ii||ife){
   int j=i+(iw?5:(ii?2:7));
   if(FC(d,n,j)<0){L++;continue;}
   int eL=FE(p,L);
   if(eL<0)thr(I,99,"missing END");
   int elL=FC2(p,L,eL,"ELSE");
   if(iw){
    unsigned long loops=0;
    while(1){
     int k=j,ok=1;
     long c=EX(I,d,n,&k,&ok);
     if(!ok||!c)break;
     TB(I,p,L+1,eL-1,dp+1);
     if(++loops>MS)thr(I,99,"loop");
    }
   }
   else if(ii){
    int k=j,ok=1;
    long c=EX(I,d,n,&k,&ok);
    if(ok){
     if(c)TB(I,p,L+1,(elL>=0?elL-1:eL-1),dp+1);
     else if(elL>=0)TB(I,p,elL+1,eL-1,dp+1);
    }
   }
   else{
    int k=j;SW(d,n,&k);
    int ll=IL(d,n,k);
    if(ll<=0){L++;continue;}
    char var[MLL];int vlen=ll<MLL-1?ll:MLL-1;
    memcpy(var,d+k,(size_t)vlen);var[vlen]=0;
    k+=ll;SW(d,n,&k);
    if(!KW(d,n,k,"IN")){L++;continue;}
    k+=2;SW(d,n,&k);
    int ll2=IL(d,n,k);
    if(ll2<=0){L++;continue;}
    char lname[MLL];int v2=ll2<MLL-1?ll2:MLL-1;
    memcpy(lname,d+k,(size_t)v2);lname[v2]=0;
    char lnm[MLM];snprintf(lnm,sizeof(lnm),"L_%s",lname);
    long lid=vg(I,lnm);
    if(lid<0||lid>=I->nlst||!I->lst[lid].alive){L++;continue;}
    List*Lt=&I->lst[lid];
    for(int k2=0;k2<Lt->len;k2++){
     if(Lt->s[k2]>=0){
      char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",var);
      vs(I,snm,Lt->s[k2]);
     }
     else AS(I,var,Lt->n[k2]);
     TB(I,p,L+1,eL-1,dp+1);
    }
   }
   L=eL+1;continue;
  }

  int ist=KW(d,n,i,"SET");int jj=i+(ist?3:0);
  if(ist)SW(d,n,&jj);
  int ll=IL(d,n,jj);
  if(ll>0&&ll<MLL){
   int aft=jj+ll;SW(d,n,&aft);
   if(ist||(aft<n&&d[aft]=='='&&!(aft+1<n&&d[aft+1]=='='))){
    char nm[MLL];memcpy(nm,d+jj,(size_t)ll);nm[ll]=0;
    int k=aft;if(!ist&&d[k]=='=')k++;
    SW(d,n,&k);
    if(k<n&&d[k]=='"'){
     int e2;int sid=SL(I,d,n,k,&e2);
     if(sid>=0){
      char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
      vs(I,snm,sid);
      AS(I,nm,sg(I,sid)->len);
      L++;continue;
     }
    }
    if(k+1<n&&d[k]=='['&&d[k+1]==']'){
     int lid=ln(I);
     char lnm[MLM];snprintf(lnm,sizeof(lnm),"L_%s",nm);
     vs(I,lnm,lid);
     AS(I,nm,0);
     L++;continue;
    }
    int ok=1;long x=EX(I,d,n,&k,&ok);
    if(ok)AS(I,nm,x);
    L++;continue;
   }
  }

  if(KW(d,n,i,"ADD")){
   int j=i+3;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;j+=ll2;
    char lnm[MLM];snprintf(lnm,sizeof(lnm),"L_%s",nm);
    long lid=vg(I,lnm);
    if(lid>=0&&lid<I->nlst&&I->lst[lid].alive){
     SW(d,n,&j);
     if(j<n&&d[j]=='"'){
      int e2;int sid=SL(I,d,n,j,&e2);
      if(sid>=0){lps(&I->lst[lid],sid);L++;continue;}
     }
     int ok=1;long x=EX(I,d,n,&j,&ok);
     if(ok)lpn(&I->lst[lid],x);
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"GET")){
   int j=i+3;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;j+=ll2;
    char lnm[MLM];snprintf(lnm,sizeof(lnm),"L_%s",nm);
    long lid=vg(I,lnm);
    if(lid>=0&&lid<I->nlst&&I->lst[lid].alive){
     int ok=1;long idx=EX(I,d,n,&j,&ok);
     List*Lt=&I->lst[lid];
     if(ok&&idx>=0&&idx<Lt->len){
      if(Lt->s[idx]>=0){
       char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
       vs(I,snm,Lt->s[idx]);
       AS(I,nm,sg(I,Lt->s[idx])->len);
      }
      else AS(I,nm,Lt->n[idx]);
     }
     else thr(I,3,"oob");
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"LEN")){
   int j=i+3;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;
    char lnm[MLM];snprintf(lnm,sizeof(lnm),"L_%s",nm);
    long lid=vg(I,lnm);
    if(lid>=0&&lid<I->nlst&&I->lst[lid].alive)AS(I,nm,I->lst[lid].len);
    else{
     char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
     long sid=vg(I,snm);
     AS(I,nm,(sid>=0&&sid<I->nstr)?sg(I,(int)sid)->len:0);
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"POP")){
   int j=i+3;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;
    char lnm[MLM];snprintf(lnm,sizeof(lnm),"L_%s",nm);
    long lid=vg(I,lnm);
    if(lid>=0&&lid<I->nlst&&I->lst[lid].alive){
     List*Lt=&I->lst[lid];
     if(Lt->len>0){
      if(Lt->s[Lt->len-1]>=0){
       char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
       vs(I,snm,Lt->s[Lt->len-1]);
      }
      else AS(I,nm,Lt->n[Lt->len-1]);
      Lt->len--;
     }
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"CLEAR")){
   int j=i+5;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;
    char lnm[MLM];snprintf(lnm,sizeof(lnm),"L_%s",nm);
    long lid=vg(I,lnm);
    if(lid>=0&&lid<I->nlst&&I->lst[lid].alive)I->lst[lid].len=0;
   }
   L++;continue;
  }

  if(KW(d,n,i,"CONCAT")){
   int j=i+6;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;j+=ll2;
    char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
    long sid1=vg(I,snm);
    if(sid1>=0&&sid1<I->nstr){
     Str*s1=sg(I,(int)sid1);int tot=s1->len;
     char*buf=sm((size_t)MLEN+1);
     if(s1->s)memcpy(buf,s1->s,(size_t)s1->len);
     for(;;){
      SW(d,n,&j);
      if(j>=n)break;
      if(d[j]=='"'){
       int e2;int sid2=SL(I,d,n,j,&e2);
       if(sid2<0)break;
       Str*s2=sg(I,sid2);
       if(tot+s2->len>MLEN){sf(I,sid2);break;}
       if(s2->s)memcpy(buf+tot,s2->s,(size_t)s2->len);
       tot+=s2->len;
       sf(I,sid2);
       j=e2;
      }
      else{
       int l3=IL(d,n,j);
       if(l3<=0)break;
       char nm2[MLL];memcpy(nm2,d+j,(size_t)l3);nm2[l3]=0;j+=l3;
       char s2n[MLM];snprintf(s2n,sizeof(s2n),"S_%s",nm2);
       long sid2=vg(I,s2n);
       if(sid2<0||sid2>=I->nstr)break;
       Str*s2=sg(I,(int)sid2);
       if(tot+s2->len>MLEN)break;
       if(s2->s)memcpy(buf+tot,s2->s,(size_t)s2->len);
       tot+=s2->len;
      }
     }
     int nid=snf(I,buf,tot);
     free(buf);
     vs(I,snm,nid);
     AS(I,nm,sg(I,nid)->len);
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"SUBSTR")){
   int j=i+6;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;j+=ll2;
    int ok=1;
    long st=EX(I,d,n,&j,&ok),lnv=EX(I,d,n,&j,&ok);
    char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
    long sid=vg(I,snm);
    if(ok&&sid>=0&&sid<I->nstr){
     Str*s=sg(I,(int)sid);
     if(st<0)st=0;
     if(lnv<0)lnv=0;
     if(st>s->len)st=s->len;
     if(st+lnv>s->len)lnv=s->len-st;
     int nid=snf(I,s->s+st,(int)lnv);
     vs(I,snm,nid);
     AS(I,nm,lnv);
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"FIND")){
   int j=i+4;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;j+=ll2;
    SW(d,n,&j);
    if(j<n&&d[j]=='"'){
     int e2;int sid2=SL(I,d,n,j,&e2);
     if(sid2>=0){
      Str*nd=sg(I,sid2);
      char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
      long sid=vg(I,snm),pos=-1;
      if(sid>=0&&sid<I->nstr){
       Str*s=sg(I,(int)sid);
       if(!nd->len)pos=0;
       else for(int k=0;k+nd->len<=s->len;k++)
        if(!memcmp(s->s+k,nd->s,(size_t)nd->len)){pos=k;break;}
      }
      AS(I,nm,pos);
      sf(I,sid2);
     }
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"REPLACE")){
   int j=i+7;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;j+=ll2;
    SW(d,n,&j);
    if(j<n&&d[j]=='"'){
     int e2;int sidf=SL(I,d,n,j,&e2);j=e2;SW(d,n,&j);
     if(sidf>=0&&j<n&&d[j]=='"'){
      int e3;int sidt=SL(I,d,n,j,&e3);
      if(sidt>=0){
       Str*sfp=sg(I,sidf);
       Str*stp=sg(I,sidt);
       char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
       long sid=vg(I,snm);
       if(sid>=0&&sid<I->nstr){
        Str*s=sg(I,(int)sid);
        char*buf=sm((size_t)MLEN+1);int bi=0,si=0;
        while(si<s->len&&bi<MLEN){
         if(sfp->len>0&&si+sfp->len<=s->len&&
            !memcmp(s->s+si,sfp->s,(size_t)sfp->len)){
          for(int q=0;q<stp->len&&bi<MLEN;q++)buf[bi++]=stp->s[q];
          si+=sfp->len;
         }
         else buf[bi++]=s->s[si++];
        }
        int nid=snf(I,buf,bi);
        free(buf);
        vs(I,snm,nid);
        AS(I,nm,bi);
       }
       sf(I,sidf);sf(I,sidt);
      }
     }
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"TO_STR")){
   int j=i+6;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;
    char b[64];
    int len=snprintf(b,sizeof(b),"%ld",vg(I,nm));
    int nid=snf(I,b,len);
    char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
    vs(I,snm,nid);
   }
   L++;continue;
  }
  if(KW(d,n,i,"TO_NUM")){
   int j=i+6;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;
    char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
    long sid=vg(I,snm);
    long val=0;
    if(sid>=0&&sid<I->nstr){Str*s=sg(I,(int)sid);val=strtol(s->s?s->s:"0",NULL,10);}
    AS(I,nm,val);
   }
   L++;continue;
  }
  if(KW(d,n,i,"UPPER")||KW(d,n,i,"LOWER")||KW(d,n,i,"TRIM")){
   int op=(d[i]=='U')?0:((d[i]=='L')?1:2);
   int j=i+(op==2?4:5);SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;
    char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
    long sid=vg(I,snm);
    if(sid>=0&&sid<I->nstr){
     Str*s=sg(I,(int)sid);
     char*buf=sm((size_t)s->len+1);
     if(s->s)memcpy(buf,s->s,(size_t)s->len);
     buf[s->len]=0;
     if(op==0){
      for(int q=0;q<s->len;q++)buf[q]=(char)toupper((unsigned char)buf[q]);
     }
     else if(op==1){
      for(int q=0;q<s->len;q++)buf[q]=(char)tolower((unsigned char)buf[q]);
     }
     else{
      int a=0,b=s->len-1;
      while(a<=b&&isspace((unsigned char)buf[a]))a++;
      while(b>=a&&isspace((unsigned char)buf[b]))b--;
      int nlen=b-a+1;
      if(nlen<0)nlen=0;
      int nid=snf(I,buf+a,nlen);
      free(buf);
      vs(I,snm,nid);
      AS(I,nm,nlen);
      L++;continue;
     }
     int nid=snf(I,buf,s->len);
     free(buf);
     vs(I,snm,nid);
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"SPLIT")){
   int j=i+5;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;j+=ll2;SW(d,n,&j);
    if(j<n&&d[j]=='"'){
     int e2;int sidsep=SL(I,d,n,j,&e2);
     if(sidsep>=0){
      Str*sep=sg(I,sidsep);
      char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
      long sid=vg(I,snm);
      if(sid>=0&&sid<I->nstr&&sep->len>0){
       Str*s=sg(I,(int)sid);
       int lid=ln(I);
       char lnm[MLM];snprintf(lnm,sizeof(lnm),"L_%s",nm);
       vs(I,lnm,lid);
       int start=0,i2=0;
       while(i2+sep->len<=s->len){
        if(!memcmp(s->s+i2,sep->s,(size_t)sep->len)){
         int part=snf(I,s->s+start,i2-start);
         lps(&I->lst[lid],part);
         i2+=sep->len;
         start=i2;
        }
        else i2++;
       }
       int part=snf(I,s->s+start,s->len-start);
       lps(&I->lst[lid],part);
      }
      sf(I,sidsep);
     }
    }
   }
   L++;continue;
  }
  if(KW(d,n,i,"JOIN")){
   int j=i+4;SW(d,n,&j);
   int ll2=IL(d,n,j);
   if(ll2>0){
    char nm[MLL];memcpy(nm,d+j,(size_t)ll2);nm[ll2]=0;j+=ll2;SW(d,n,&j);
    if(j<n&&d[j]=='"'){
     int e2;int sidsep=SL(I,d,n,j,&e2);
     if(sidsep>=0){
      Str*sep=sg(I,sidsep);
      char lnm[MLM];snprintf(lnm,sizeof(lnm),"L_%s",nm);
      long lid=vg(I,lnm);
      if(lid>=0&&lid<I->nlst&&I->lst[lid].alive){
       List*Lt=&I->lst[lid];
       char*buf=sm((size_t)MLEN+1);int bi=0;
       for(int k=0;k<Lt->len;k++){
        if(k)for(int q=0;q<sep->len&&bi<MLEN;q++)buf[bi++]=sep->s[q];
        if(Lt->s[k]>=0){
         Str*p=sg(I,Lt->s[k]);
         for(int q=0;q<p->len&&bi<MLEN;q++)buf[bi++]=p->s[q];
        }
        else{
         char t2[64];int tn=snprintf(t2,sizeof(t2),"%ld",Lt->n[k]);
         for(int q=0;q<tn&&bi<MLEN;q++)buf[bi++]=t2[q];
        }
       }
       int nid=snf(I,buf,bi);
       free(buf);
       char snm[MLM];snprintf(snm,sizeof(snm),"S_%s",nm);
       vs(I,snm,nid);
       AS(I,nm,bi);
      }
      sf(I,sidsep);
     }
    }
   }
   L++;continue;
  }

  if(KW(d,n,i,"SQ")){I->r[0]=cv(I->r[0]*I->r[0]);L++;continue;}
  if(KW(d,n,i,"COUT")||KW(d,n,i,"IO")){printf("%ld\n",I->r[0]);L++;continue;}
  if(KW(d,n,i,"BOOL")){I->r[0]=I->r[0]!=0;L++;continue;}
  if(KW(d,n,i,"TRO")||KW(d,n,i,"FOT")){
   int j=i+3;SW(d,n,&j);long a;
   if(NP(d,n,&j,&a)){
    long x=1,y=2;
    for(int k=0;k<a;k++){long z=x+y;x=y;y=z;}
    printf("%ld\n",x);
   }
   L++;continue;
  }
  if(KW(d,n,i,"GH")){
   int j=i+2;SW(d,n,&j);long a;
   if(NP(d,n,&j,&a))if(a>=0&&a<(long)I->tsz)tset(I,a,(I->r[0]>>8)&255);
   L++;continue;
  }
  if(KW(d,n,i,"JO")){
   int j=i+2;SW(d,n,&j);long a;
   if(NP(d,n,&j,&a))if(I->r[0]&&a>=0&&a<p->n){L=(int)a;continue;}
   L++;continue;
  }
  if(KW(d,n,i,"POI")){
   int j=i+3;SW(d,n,&j);long a;
   if(NP(d,n,&j,&a))I->ptr+=a;
   if(I->ptr<0)I->ptr=0;
   if((size_t)I->ptr>I->tsz-1)I->ptr=(long)I->tsz-1;
   L++;continue;
  }
  if(KW(d,n,i,"IU")){
   int c=getchar();
   if(c!=EOF&&c>='a'&&c<='z')c-=32;
   I->r[0]=c==EOF?0:c;L++;continue;
  }
  if(KW(d,n,i,"LOU")){
   int c=getchar();
   if(c!=EOF&&c>='A'&&c<='Z')c+=32;
   I->r[0]=c==EOF?0:c;L++;continue;
  }
  if(KW(d,n,i,"ASD")){I->r[0]=cv(I->r[0]+48);L++;continue;}
  if(KW(d,n,i,"IOL")){putchar((int)(I->r[0]&255));putchar('\n');L++;continue;}
  if(KW(d,n,i,"BOlN")){
   for(int k=15;k>=0;k--)putchar((I->r[0]>>k)&1?'1':'0');
   putchar('\n');
   L++;continue;
  }

  if(KW(d,n,i,"PRINT")){int j=i+5;DOP(I,d,n,&j);L++;continue;}

  if(d[i]=='G'&&i+1<n&&isdigit((unsigned char)d[i+1])){
   int j=i+1;long a;
   if(NP(d,n,&j,&a)){if(a>=0&&a<(long)I->tsz)I->r[0]=tget(I,a);L++;continue;}
  }
  if(d[i]=='W'&&i+1<n&&isdigit((unsigned char)d[i+1])){
   int j=i+1;long a;
   if(NP(d,n,&j,&a)){if(a>=0&&a<(long)I->tsz)tset(I,a,(int)(I->r[0]&255));L++;continue;}
  }
  if(d[i]=='M'&&i+1<n&&isdigit((unsigned char)d[i+1])){
   int j=i+1;long a;
   if(NP(d,n,&j,&a)){I->r[1]=I->r[0];I->r[0]=cv(a);L++;continue;}
  }
  if(d[i]=='C'&&i+1<n&&isdigit((unsigned char)d[i+1])){
   int j=i+1;long a;
   if(NP(d,n,&j,&a)){if(a>=0&&a<p->n)TB(I,p,(int)a,(int)a,dp+1);L++;continue;}
  }
  if(d[i]=='B'&&i+1<n&&isdigit((unsigned char)d[i+1])){
   int j=i+1;long a;
   if(NP(d,n,&j,&a)){if(I->r[0]&&a>=0&&a<p->n)TB(I,p,(int)a,(int)a,dp+1);L++;continue;}
  }
  if(d[i]=='Z'&&i+1<n&&isdigit((unsigned char)d[i+1])){
   int j=i+1;long a;
   if(NP(d,n,&j,&a)){if(!I->r[0]&&a>=0&&a<p->n)TB(I,p,(int)a,(int)a,dp+1);L++;continue;}
  }
  if(d[i]=='S'&&i+1<n&&isdigit((unsigned char)d[i+1])){
   int j=i+1;long a;
   if(NP(d,n,&j,&a)){if(a>=0&&a<16)I->r[a]=I->r[0];L++;continue;}
  }

  if(d[i]=='P'||d[i]=='p'||d[i]=='0'||d[i]=='1'||d[i]=='2'){
   long w=0;int bt=0,j=i;
   for(int k=0;k<10;k++){
    int bf2=j;int t=-1;
    if(j<n){
     char cc=d[j];
     if(cc=='0'){t=0;j++;}
     else if(cc=='1'){t=1;j++;}
     else if(cc=='2'){t=2;j++;}
     else if(cc=='P'||cc=='p'){t=3;j++;}
    }
    if(t<0){j=bf2;break;}
    w=w*4+t;bt++;
   }
   if(bt>0){int op=(int)(w%106)+1;vo(I,op,0);}
   L++;continue;
  }
  if(isdigit((unsigned char)d[i])){
   long x;int j=i;
   if(NP(d,n,&j,&x))I->r[0]=cv(x);
   L++;continue;
  }

  fprintf(stderr,"[TELL] ?line %d: %s\n",L+1,d);
  L++;
 }
}

/* ---------- main ---------- */
static unsigned char*LD(const char*pt,long*n){
 FILE*f=fopen(pt,"rb");
 if(!f)return NULL;
 fseek(f,0,SEEK_END);
 long sz=ftell(f);
 fseek(f,0,SEEK_SET);
 if(sz<=0){fclose(f);return NULL;}
 unsigned char*b=sm((size_t)sz);
 if(fread(b,1,(size_t)sz,f)!=(size_t)sz){free(b);fclose(f);return NULL;}
 fclose(f);
 *n=sz;
 return b;
}
static size_t ptape(const char*a,size_t d){
 if(!a)return d;
 if(!strncmp(a,"--tape=",7)){
  const char*p=a+7;char*e=NULL;
  unsigned long v=strtoul(p,&e,10);
  if(e==p)return d;
  if(*e=='M'||*e=='m')return(size_t)v*1024*1024;
  if(*e=='K'||*e=='k')return(size_t)v*1024;
  return(size_t)v*1024;
 }
 return d;
}

static void* run_thread(void* arg) {
    char* code = (char*)arg;
    // Создаем новую VM для потока
    I I;
    iinit(&I, TAPE_DEF);
    TL p;
    memset(&p, 0, sizeof(p));
    tbu((unsigned char*)code, strlen(code), &p);
    
    // Запускаем исполнение
    EF top; top.prev = NULL; I.err = NULL; tpu(&I, &top);
    if (setjmp(top.jb) == 0) {
        TB(&I, &p, 0, p.n - 1, 0);
    } else {
        fprintf(stderr, "[THREAD PANIC] %ld: %s\n", top.code, top.msg);
    }
    tpo(&I);
    tfr(&p);
    ifree(&I);
    return NULL;
}

int main(int c,char**v){
 isig();
 if(c<2||c>3){fprintf(stderr,"usage: %s <file.tell> [--tape=N[M|K]]\n",v[0]);return 1;}
 size_t sz=ptape(c==3?v[2]:NULL,TAPE_DEF);
 I I;iinit(&I,sz);
 long n=0;
 unsigned char*d=LD(v[1],&n);
 if(!d){ifree(&I);return 1;}
 TL p;memset(&p,0,sizeof(p));
 tbu(d,n,&p);

 for(int L=0;L<p.n;L++){
  const char*dd=p.l[L];int nn=(int)strlen(dd);int q=0;
  SW(dd,nn,&q);
  if(q>=nn)continue;
  if(KW(dd,nn,q,"LABEL")){
   int j=q+5;SW(dd,nn,&j);
   int ll=IL(dd,nn,j);
   if(ll>0&&ll<MLL){
    char nm[MLL];memcpy(nm,dd+j,(size_t)ll);nm[ll]=0;
    la(&I,nm,L);
   }
  }
  else if(KW(dd,nn,q,"dow")){
   int j=q+3;SW(dd,nn,&j);
   if(j<nn&&dd[j]=='('){
    j++;int js=j;
    while(j<nn&&dd[j]!=')')j++;
    if(j<nn){
     char nm[MLL];int ll=j-js;if(ll>MLL-1)ll=MLL-1;
     memcpy(nm,dd+js,(size_t)ll);nm[ll]=0;
     la(&I,nm,L);
    }
   }
  }
  else if(KW(dd,nn,q,"HOL_JMP")){
   int j=q+7;SW(dd,nn,&j);
   if(j<nn&&dd[j]==':')j++;
   int k=j;SW(dd,nn,&k);
   if(k<nn&&dd[k]=='(')hparse(&I,dd+j,nn-j);
   else{
    int K=L+1;
    while(K<p.n){
     const char*ee=p.l[K];int en=(int)strlen(ee);int qq=0;
     SW(ee,en,&qq);
     if(qq>=en){K++;continue;}
     if(isdigit((unsigned char)ee[qq])){
      int q3=qq;
      while(q3<en&&isdigit((unsigned char)ee[q3]))q3++;
      if(q3<en&&ee[q3]=='_'){hparse(&I,ee,en);K++;continue;}
     }
     break;
    }
    L=K-1;
   }
  }
 }

 EF top;top.prev=NULL;I.err=NULL;tpu(&I,&top);
 int rc=0;
 if(setjmp(top.jb)==0){TB(&I,&p,0,p.n-1,0);}
 else{fprintf(stderr,"[UNCAUGHT] %ld: %s\n",top.code,top.msg);tpo(&I);rc=2;}
 tpo(&I);
 tfr(&p);free(d);ifree(&I);
 return rc;
}
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#define LAB7_THREAD_2

#define NBUCKET 5
#define NKEYS 100000
#ifdef LAB7_THREAD_2
pthread_mutex_t lock[NBUCKET];
#endif
struct entry {
  int key;
  int value;
  struct entry *next;
};
struct entry *table[NBUCKET];
int keys[NKEYS];
int nthread = 1;

double
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void 
insert(int key, int value, struct entry **p, struct entry *n)  // 这个像是头插法
{
  struct entry *e = malloc(sizeof(struct entry));
  e->key = key;
  e->value = value;
  e->next = n;
  *p = e;
}

static 
void put(int key, int value)
{
  int i = key % NBUCKET;  // 插入第i个bucket
#ifdef LAB7_THREAD_2
  //pthread_mutex_lock(&lock);
  pthread_mutex_lock(&lock[i]);
#endif
  // is the key already present?
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
#ifdef LAB7_THREAD_2
  //pthread_mutex_unlock(&lock);
  pthread_mutex_unlock(&lock[i]);
#endif
}

static struct entry*
get(int key)
{
  int i = key % NBUCKET;

  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key) break;
  }

  return e;
}

static void *
put_thread(void *xa)  // 把100000分成nthread部分，比如nthread为2，则分为前50000和后50000，前50000用线程0插入，后50000用线程1插入
{
  int n = (int) (long) xa; // thread number
  int b = NKEYS/nthread;

  for (int i = 0; i < b; i++) {
    put(keys[b*n + i], n);
  }

  return NULL;
}

static void *
get_thread(void *xa)
{
  int n = (int) (long) xa; // thread number
  int missing = 0;
  //int b = NKEYS/nthread;

  for (int i = 0; i < NKEYS; i++) {   // 这里为什么没有用多线程去读
    struct entry *e = get(keys[i]);
    if (e == 0) missing++;
  }
 /*
  for(int i=0;i<b;i++){
    struct entry *e = get(keys[b*n + i]);
    if(e==0) missing++;
  }
*/
  printf("%d: %d keys missing\n", n, missing);
  return NULL;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s nthreads\n", argv[0]);
    exit(-1);
  }
#ifdef LAB7_THREAD_2
  //pthread_mutex_init(&lock, NULL);
  //降低锁的粒度
  for(int i = 0;i < NBUCKET; i++){
    pthread_mutex_init(&lock[i], NULL);
  }
#endif

  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);
  assert(NKEYS % nthread == 0);
  for (int i = 0; i < NKEYS; i++) {
    keys[i] = random();
  }

  //
  // first the puts
  //
  t0 = now();
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, put_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < nthread; i++) {  // 循环执行所有put线程
    assert(pthread_join(tha[i], &value) == 0);  
  }
  t1 = now();

  printf("%d puts, %.3f seconds, %.0f puts/second\n",
         NKEYS, t1 - t0, NKEYS / (t1 - t0));

  //
  // now the gets
  //
  t0 = now();
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, get_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d gets, %.3f seconds, %.0f gets/second\n",
         NKEYS*nthread, t1 - t0, (NKEYS*nthread) / (t1 - t0));
}

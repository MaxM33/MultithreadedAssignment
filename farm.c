#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_STRING_SIZE 255
#define SOCKNAME "./farm.sck"
#define UNIX_PATH_MAX 108

typedef struct {
  long result;
  char filename[MAX_STRING_SIZE];
} Datastruct;

void *func(void *arg);

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex3 = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t fullq = PTHREAD_COND_INITIALIZER; // condition variable

struct sockaddr_un server;
int qlen = 8, activeWorkers = 0, numfiles = 0, fd_skt, delay = 0;
char **files;

// when signal is recieved, execution of the program is forced to end
void sighandler(int sign) {
  pthread_mutex_lock(&mutex1);
  numfiles = 0;
  pthread_mutex_unlock(&mutex1);
}

int main(int argc, char *argv[]) {

  if (argc == 1) { // no args, program terminated
    printf("Must specify a list of files\n");
    exit(EXIT_FAILURE);
  }

  // install SIGHUP, SIGINT, SIGQUIT, SIGTERM handlers
  sigset_t set;
  struct sigaction s;
  if (sigfillset(&set) == -1) {
    perror("sigfillset() failed");
    exit(EXIT_FAILURE);
  }
  if (pthread_sigmask(SIG_SETMASK, &set, NULL) == -1) { // blocking all signals
    perror("sigmask() failed");
    exit(EXIT_FAILURE);
  }
  if (sigemptyset(&set) == -1) {
    perror("sigemptyset() failed");
    exit(EXIT_FAILURE);
  }
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGQUIT);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGHUP);
  s.sa_mask = set;
  s.sa_handler = &sighandler;
  s.sa_flags = SA_RESTART;
  if (sigaction(SIGINT, &s, NULL) == -1) {
    perror("sigaction1() failed");
    exit(EXIT_FAILURE);
  }
  if (sigaction(SIGQUIT, &s, NULL) == -1) {
    perror("sigaction2() failed");
    exit(EXIT_FAILURE);
  }
  if (sigaction(SIGHUP, &s, NULL) == -1) {
    perror("sigaction3() failed");
    exit(EXIT_FAILURE);
  }
  if (sigaction(SIGTERM, &s, NULL) == -1) {
    perror("sigaction4() failed");
    exit(EXIT_FAILURE);
  }
  if (sigemptyset(&set) == -1) {
    perror("sigemptyset() failed");
    exit(EXIT_FAILURE);
  }
  if (pthread_sigmask(SIG_SETMASK, &set, NULL) == -1) { // no signal is blocked
    perror("sigmask() failed");
    exit(EXIT_FAILURE);
  }

  int threadnum = 4, j = 0;
  files = (char **)malloc(sizeof(char *));
  struct stat *buf;
  buf = (struct stat *)malloc(sizeof(struct stat));

  // program menu options: -n, -q, -t
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-n") == 0) {
      threadnum = atoi(argv[i + 1]); // sets number of threads, default is 4
      i++;
    } else if (strcmp(argv[i], "-q") == 0) {
      qlen = atoi(argv[i + 1]); // sets queue length, default is 8
      i++;
    } else if (strcmp(argv[i], "-t") == 0) {
      delay = atoi(argv[i + 1]); // sets delay, default is 0
      i++;
    } else {
      if (strlen(argv[i]) > MAX_STRING_SIZE) { // skips if filename length > 255
        // printf("Maximum file name length exceeded\n");
        continue;
      }
      if (stat(argv[i], buf) == -1) { // stat to check 'st_mode'
        perror("stat() failed in main");
        exit(EXIT_FAILURE);
      }
      if (!S_ISREG(buf->st_mode)) { // skips file if not regular
        // printf("File '%s' is not regular\n", argv[i]);
        continue;
      }
      // reallocs memory for another string in the list of files (multi-D array)
      files = (char **)realloc(files, (j + 1) * sizeof(char *));
      // adjusts the allocated string size;
      files[j] = (char *)malloc(strlen(argv[i]) * sizeof(char) + 1);
      strcpy(files[j], argv[i]);
      j++;
    }
  }
  numfiles = j;
  free(buf);

  strncpy(server.sun_path, SOCKNAME, UNIX_PATH_MAX);
  server.sun_family = AF_UNIX;

  pid_t Collector = fork();
  if (Collector == 0) {
    sigset_t set2;
    int s_sck, fd_c;
    Datastruct collected;

    char prevbadoutput[MAX_STRING_SIZE] = ".";

    // blocking SIGHUP, SIGINT, SIGQUIT, SIGTERM
    if (sigemptyset(&set2) == -1) {
      perror("sigemptyset() failed");
      exit(EXIT_FAILURE);
    }
    sigaddset(&set2, SIGINT);
    sigaddset(&set2, SIGQUIT);
    sigaddset(&set2, SIGTERM);
    sigaddset(&set2, SIGHUP);
    if (pthread_sigmask(SIG_BLOCK, &set2, NULL) == -1) {
      perror("sigmask() failed");
      exit(EXIT_FAILURE);
    }

    /*
      freeing the multidimensional array containing files, dont need here in
      collector. It couldn't be done before forking because the list of files is
      created when taking the parameters, and there are some other data that is
      useful to be shared to the child process. So it is freed after instead.
    */
    for (int i = 0; i < j; i++) {
      free(files[i]);
    }
    free(files);

    s_sck = socket(AF_UNIX, SOCK_STREAM, 0);
    bind(s_sck, (struct sockaddr *)&server, sizeof(server));
    listen(s_sck, SOMAXCONN);
    fd_c = accept(s_sck, NULL, 0);

    while (j > 0) {
      int ret = read(fd_c, &collected, sizeof(Datastruct));
      if (ret == -1) {
        perror("write() failed in thread");
        exit(EXIT_FAILURE);
      }
      /*
        When a signal is recieved by masterworker, if there are threads that are
        already in queue (usually it's the case), those threads finish to work
        on the files and send results to this process (collector), while the
        remaining threads are terminated. Some garbage data still remains on the
        socket, and it is filtered by this strcmp().
      */
      if (strcmp(collected.filename, prevbadoutput)) {
        printf("%ld %s\n", collected.result, collected.filename);
      }
      strcpy(prevbadoutput, collected.filename);
      j--;
    }
    close(fd_c);
    close(s_sck);
    unlink(SOCKNAME);
    exit(EXIT_SUCCESS);
  }

  fd_skt = socket(AF_UNIX, SOCK_STREAM, 0);

  while (connect(fd_skt, (struct sockaddr *)&server, sizeof(server)) == -1) {
    if (errno == ENOENT) {
      sleep(1); // listen() not called yet, wait 1 sec and re-try till connected
    } else {
      perror("connect() failed");
      unlink(SOCKNAME); // connect failed so it deletes the socket file
      exit(EXIT_FAILURE);
    }
  }
  // masterworker and collector are connected now

  // creating and waiting all the thread workers
  pthread_t tid[threadnum];
  for (int i = 0; i < threadnum; i++) {
    pthread_create(&tid[i], NULL, func, NULL);
  }
  for (int i = 0; i < threadnum; i++) {
    pthread_join(tid[i], NULL);
  }

  close(fd_skt);
  unlink(SOCKNAME); // deletes the socket file

  // frees multidimensional array
  for (int i = 0; i < j; i++) {
    free(files[i]);
  }
  free(files);

  return 0;
}

void *func(void *arg) {
  int filesize = 0;
  long num = 0, result = 0;
  char *processedfile;
  struct stat *buf;
  Datastruct data = {};
  FILE *fd;
  while (1) {
    pthread_mutex_lock(&mutex1);
    if (numfiles <= 0) { // checks if there are any files left in the array...
      pthread_mutex_unlock(&mutex1);
      pthread_exit(NULL); // if not, thread is terminated
    }
    processedfile = (char *)malloc(strlen(files[numfiles - 1]) * sizeof(char) + 1);
    strcpy(processedfile, files[numfiles - 1]); // gets filename
    numfiles--;
    while (activeWorkers >= qlen) {       // if queue is full...
      pthread_cond_wait(&fullq, &mutex1); // ...thread goes in waiting
    }
    activeWorkers++; // thread is actually working now
    pthread_mutex_unlock(&mutex1);

    pthread_mutex_lock(&mutex2);  // lock so each thread waits the delay one by one.
    usleep(delay * 1000); // worker waits delay time (in milliseconds)
    pthread_mutex_unlock(&mutex2);

    buf = (struct stat *)calloc(1, sizeof(struct stat));

    if (stat(processedfile, buf) == -1) { // stat to get file size
      perror("stat() failed in thread");
      exit(EXIT_FAILURE);
    }
    filesize = buf->st_size;

    free(buf); // free dynamic memory used

    fd = fopen(processedfile, "rb"); // opens binary file
    if (!fd) {
      perror("fopen() failed");
      exit(EXIT_FAILURE);
    }
    /*
    gets one long at a time from the file, which is multiplied by i and summed
    to the final result
    */
    fseek(fd, 0, SEEK_SET);
    for (int i = 0; i < filesize / sizeof(long); i++) {
      fread(&num, sizeof(long), 1, fd);
      result = result + (i * num);
    }
    fclose(fd);

    // copying results to custom structure
    strcpy(data.filename, processedfile);
    data.result = result;

    int ret = write(fd_skt, &data, sizeof(data)); // sends results to collector
    if (ret == -1) {
      perror("write() failed in thread");
      exit(EXIT_FAILURE);
    }

    free(processedfile); // free dynamic memory used
    result = 0;
    /*
    thread gets lock and, as it finishes working by sending the result to
    collector, signals to unlock the first thread in the waiting queue, then
    lock is released
    */
    pthread_mutex_lock(&mutex3);
    activeWorkers--;
    pthread_cond_signal(&fullq);
    pthread_mutex_unlock(&mutex3);
  }
}
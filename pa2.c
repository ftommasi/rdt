#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/types.h>
#include <netinet/in.h>

/* ******************************************************************
   This code should be used for PA2, unidirectional data transfer protocols 
   (from A to B)
   Network properties:
   - one way network delay averages five time units (longer if there
     are other messages in the channel for Pipelined ARQ), but can be larger
   - packets can be corrupted (either the header or the data portion)
     or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
     (although some can be lost).
**********************************************************************/

/* a "msg" is the data unit passed from layer 5 (teachers code) to layer  */
/* 4 (students' code).  It contains the data (characters) to be delivered */
/* to layer 5 via the students transport level protocol entities.         */
struct msg {
  char data[20];
  };

/* a packet is the data unit passed from layer 4 (students code) to layer */
/* 3 (teachers code).  Note the pre-defined packet structure, which all   */
/* students must follow. */
struct pkt {
   int  seqnum;
   int  acknum;
   int  checksum;
   char payload[20];
};


/*- Your Definitions 
  
  
  ---------------------------------------------------------------------------*/

//C doesn't support bool so use char for least space
#define bool char 
#define BUFFER_SIZE 1050
#define DEBUG 1
//for stats
int num_original_packets;
int num_retransmissions;
int num_acks;
int num_corrupted_recvd;
int total_num_corrupted = 0;
int total_lost = 0;
double* avg_rtt;
//


struct pkt* A_buffer;
struct pkt* B_buffer;

struct pkt in_travel;
struct pkt* A_in_travel_buffer;

double* A_packet_timers;

int A_next_packet;
int B_next_packet;

bool* A_buffer_acks;
bool* B_buffer_acks;

bool A_ready_to_send;
bool A_timer_set;

bool B_inserted;

int A_curr_seqno;
int B_curr_seqno;
int A_next_buffer_index;
int B_next_buffer_index;

int A_curr_acknum;
int B_curr_acknum;

int A_window_base;
int A_window_end;

int B_window_base;
int B_window_end;


/* Please use the following values in your program */

#define   A    0
#define   B    1
#define   FIRST_SEQNO   0

/*- Declarations ------------------------------------------------------------*/
void	restart_rxmt_timer(void);
void	tolayer3(int AorB, struct pkt packet);
void	tolayer5(char datasent[20]);

void	starttimer(int AorB, double increment);
void	stoptimer(int AorB);

/* WINDOW_SIZE, RXMT_TIMEOUT and TRACE are inputs to the program;
   We have set an appropriate value for LIMIT_SEQNO.
   You do not have to concern but you have to use these variables in your 
   routines --------------------------------------------------------------*/

extern int WINDOW_SIZE;      // size of the window
extern int LIMIT_SEQNO;      // when sequence number reaches this value,                                     // it wraps around
extern double RXMT_TIMEOUT;  // retransmission timeout
extern int TRACE;            // trace level, for your debug purpose
extern double time_now;      // simulation time, for your debug purpose

/********* YOU MAY ADD SOME ROUTINES HERE ********/

int 
calculate_checksum(int seqnum, int acknum, char* payload)

{
  int checksum_result = seqnum + acknum;
  if(payload){ 
    int i;
    for(i=0; i < 20; i++){
      checksum_result+= (int)payload[i];
    }
  }

  return checksum_result;
}


void dumpA(){
  printf("-----------------------A DUMP--------------------------------\n");
  int i;
  printf("start: %d end %d\n", A_window_base, A_window_end);
  for(i=0; i < BUFFER_SIZE; i++){
    printf("%d {%c,%d,%d,%d}  | ", i, A_buffer[i].payload[0],A_buffer[i].seqnum,A_buffer[i].acknum,A_buffer_acks[i]);
    
  }

  printf("\n");
  printf("-----------------------/A DUMP--------------------------------\n");
}

void dumpB(){
  printf("-----------------------B DUMP--------------------------------\n");
  int i;
  printf("start: %d end %d\n", B_window_base, B_window_end);
  for(i=0; i < BUFFER_SIZE; i++){
    printf("%d {%c,%d,%d,%d}  | ", i, B_buffer[i].payload[0],B_buffer[i].seqnum,B_buffer[i].acknum,B_buffer_acks[i]);
  }
  printf("\n");
  printf("-----------------------/B DUMP--------------------------------\n");
}

void
dump_packet(packet)
  struct pkt packet;
{
  printf("%d,%d,%d,%s\n",packet.seqnum, packet.acknum, packet.checksum, packet.payload);

}


/********* STUDENTS WRITE THE NEXT SEVEN ROUTINES *********/



/* called from layer 5, passed the data to be sent to other side */
void
A_output (message)
    struct msg message;
{
  if(DEBUG){ 
    printf("A_out called ");
  if(A_ready_to_send){
    printf("-ready-");
  }else{
    printf("-NOT ready-");
  }
  printf(" payload: %s \n",message.data);
  }
  struct pkt packet;
  packet.seqnum = A_curr_seqno;         
  packet.acknum = A_curr_acknum;
  memcpy(packet.payload,message.data,20);
  packet.checksum = calculate_checksum(packet.seqnum, packet.acknum, packet.payload);

  A_curr_seqno ++;
  A_curr_acknum++;
  num_original_packets++;

  A_buffer[A_next_buffer_index] = packet;
  A_buffer_acks[A_next_buffer_index] = 0;
  A_next_buffer_index = (A_next_buffer_index+1)%BUFFER_SIZE;
  if(DEBUG){
  printf("A_next_packet: %d, A: ", A_next_packet);
  dumpA();  
  }
  if(A_ready_to_send){
    if(!A_timer_set){
      if(DEBUG) printf("A out is starting timer.\n");
      starttimer(A,RXMT_TIMEOUT);
      A_timer_set = 1;
    }
    if(DEBUG){
      printf("A sent %d: ",A_next_packet);
    dump_packet(A_buffer[A_next_packet]);
    }
    in_travel = A_buffer[A_next_packet];
    tolayer3(A,A_buffer[A_next_packet]);
    A_packet_timers[A_next_packet] = time_now;
    A_next_packet = (A_next_packet+1)%BUFFER_SIZE; 
    //start the timer for this packet that was sent.
    if(A_next_packet > A_window_end){
      A_ready_to_send = 0; 
    }

  }

}
/* called from layer 3, when a packet arrives for layer 4 */
void
A_input(packet)
  struct pkt packet;
{
  if(DEBUG){
    printf("ACK PAYLOAD '%s'\n",packet.payload);
    printf("len: %lu, strcmp('?'): %d, checksum: %d(%d) seq: %d, ack: %d\n", strlen(packet.payload),strcmp(packet.payload,"?"),packet.checksum,calculate_checksum(packet.seqnum,packet.acknum,NULL),packet.seqnum,packet.acknum);
  }
  if(packet.checksum == calculate_checksum(packet.seqnum,packet.acknum,NULL) && strlen(packet.payload) == 0){
  if(DEBUG){
    printf("A_in called\n");
  //slide window
  printf("sliding window\n");
  }
  if(A_buffer_acks[A_window_base]){
    int i;
    int next_unacked = 0;
    for(i=A_window_base; i != A_window_end; i = (i+1) % BUFFER_SIZE){
      if(!A_buffer_acks[i]) break;
      next_unacked++;
    }
    A_ready_to_send = (next_unacked > 0 ? 1 : 0); 
    A_window_base = (A_window_base + next_unacked) % BUFFER_SIZE;
    A_window_end = (A_window_end + next_unacked) % BUFFER_SIZE;
  }
  
  int i;
  
  if(DEBUG)printf("updating acked packets up to %d from %d - %d | %d\n",packet.acknum,A_window_base, A_window_end, A_next_buffer_index);
  for(i=A_window_base; i != A_window_end && i < A_next_buffer_index; i = (i+1) % BUFFER_SIZE){
     if(packet.seqnum == A_buffer[i].seqnum){
      A_packet_timers[i] = time_now - A_packet_timers[i];
     } 
    if(packet.acknum >= A_buffer[i].acknum ){
        if(DEBUG)printf("all packet up to %d are acked\n", packet.acknum);
        //A_packet_timers[i] = time_now;
        A_buffer_acks[i] = 1;//has been acked
        //A_next_packet = (A_next_packet + 1) % BUFFER_SIZE;
      }
      //keep track of which has been acked
      //stopping timer multiple times in a row
      //stoptimer(A);//stop timer for this packet cause it has been acked
      //starttimer(A,RXMT_TIMEOUT);
    }
  }else{
    num_corrupted_recvd++;
    if(DEBUG)printf("ack packet corrupted\n");
  }
}

/* called when A's timer goes off */
void
A_timerinterrupt (void)
{
  int packet_timeout = -1;
  int i;
  bool break_flag = 0;
  if(DEBUG){
  printf("adjusting times\n");
  printf("A TIMERINTERRUPT IS BEING CALLED\n");
  printf("checking what to retransmit (%d - %d)\n",A_window_base,A_window_end+1);
  }
  for(i=A_window_base; i != A_window_end && i < A_next_buffer_index; i = (i+1) % BUFFER_SIZE){
    A_packet_timers[i] = time_now - A_packet_timers[i];
  }
  
    //restransmit unacked packet
  for(i=A_window_base; i != (A_window_end) && i < A_next_buffer_index ;  i = (i+1) % BUFFER_SIZE){
  //for(i=0; i <BUFFER_SIZE-1;  i = (i+1) % BUFFER_SIZE){
    //printf("%.5f | ",A_packet_timers[i]);
    if(!A_buffer_acks[i]){
      if(DEBUG)printf("retransmitting  %s", A_buffer[i].payload);
      A_packet_timers[i] = time_now;
      num_retransmissions++;  
      tolayer3(A, A_buffer[i]);
      break;
    }
  }
  if(DEBUG)printf("\n");
  //restart timer for this packet
  starttimer(A,RXMT_TIMEOUT );
} 

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void
A_init (void)
{

  if(DEBUG)printf("A INIT IS BEING CALLED\n");
  A_buffer = (struct pkt*) malloc(BUFFER_SIZE * sizeof(struct pkt));
  A_buffer_acks = (bool*) malloc(BUFFER_SIZE * sizeof(bool));
  A_in_travel_buffer = (struct pkt*) malloc(WINDOW_SIZE * sizeof(struct pkt));
  A_packet_timers = (double*) malloc(BUFFER_SIZE* sizeof(double)); 
  avg_rtt = (double*) malloc(BUFFER_SIZE * sizeof(double));
  int i;
  struct pkt dummy;
  dummy.seqnum = -1;
  dummy.acknum = -1;
  memcpy(dummy.payload,"-------------------\n",20);
  for(i =0; i < BUFFER_SIZE; i++){
   A_buffer[i] = dummy;
   A_buffer_acks[i] = 0;
   A_packet_timers[i] = 0;
  }

  A_curr_seqno = FIRST_SEQNO;
  A_next_buffer_index = 0;
  A_curr_acknum = FIRST_SEQNO;
  A_ready_to_send = 1;
  A_timer_set = 0;
  A_window_base = 0;
  A_window_end = WINDOW_SIZE-1;

  num_original_packets = 0;
  num_retransmissions = 0;

} 

/* called from layer 3, when a packet arrives for layer 4 at B*/
void
B_input (packet)
    struct pkt packet;
{
  int generated_acknums[WINDOW_SIZE];
  if(DEBUG)printf("B_in called\n");
  struct pkt ack_packet;
  bool duplicate = 0;
  bool in_window = 1;
    if(packet.checksum == calculate_checksum(packet.seqnum, packet.acknum, packet.payload)){
    int i;
    //for(i = 0;  i < B_next_buffer_index; i++){
    for(i = B_window_base; i != B_window_end ; i = (i+1) % BUFFER_SIZE){
      if(packet.seqnum < B_buffer[B_window_base].seqnum || packet.seqnum > B_buffer[B_window_end].seqnum){
        in_window = 0;
        if(DEBUG)
          printf("detected a packet out of window[%d - %d]. Acking {%c %d %d}"
              ,B_buffer[B_window_base].seqnum,packet.seqnum > B_buffer[B_window_end].seqnum,packet.payload[0],packet.seqnum,packet.acknum);
      }
      if(B_buffer_acks[i] && B_buffer[i].seqnum == packet.seqnum && B_buffer[i].acknum == packet.acknum ){
        //we've already acked this packet. re ack only
        duplicate = 1;
        if(DEBUG)printf("detected a previously acked packet at[%d-%d] %d. Acking{%c %d %d,%d}\n",B_window_base,B_window_end,i,packet.payload[0],packet.seqnum,packet.acknum,B_buffer_acks[i]);
      }
    }
    if(!B_inserted){
      B_inserted = 1;
      duplicate = 0;
      in_window = 1;
    }
    if(!duplicate && in_window){    
      if(DEBUG)printf("B ACKED new packet %s\nInserting into %d",packet.payload,B_next_buffer_index);
      B_next_buffer_index = packet.seqnum%BUFFER_SIZE; //= (packet.seqnum/20)%BUFFER_SIZE;
      //B_buffer[B_next_buffer_index] = packet;//buffer packet to detect duplicates
      B_buffer[B_next_buffer_index] = packet;//buffer packet to detect duplicates
      B_buffer_acks[B_next_buffer_index] = 1;//buffer packet to detect duplicates
      B_next_buffer_index++; 
      for(i = B_window_base; i != B_window_end ; i = (i+1) % BUFFER_SIZE){
        if(!B_buffer_acks[i])break;
        B_curr_seqno++;
      }
    }
    if(DEBUG){
      printf("B %d: ", B_next_buffer_index);
    dumpB();
    }
    ack_packet.seqnum = packet.seqnum;         
    ack_packet.acknum = B_curr_seqno;
    //memcpy(ack_packet.payload,0,20);
    ack_packet.checksum = calculate_checksum(ack_packet.seqnum, ack_packet.acknum, NULL);
  

    //slide window
    if(B_buffer_acks[B_window_base]){
      int i;
      int next_unacked = 0;
      for(i=B_window_base; i != B_window_end; i = (i+1) % BUFFER_SIZE){
        if(!B_buffer_acks[i]) break;
        next_unacked++;
      }
      //write to layer 5 as packet slide out of window
      for(i=B_window_base; i != B_window_base+next_unacked; i = (i+1) % BUFFER_SIZE){
        tolayer5(B_buffer[i].payload);
      }
      B_window_base = (B_window_base + next_unacked) % BUFFER_SIZE;
      B_window_end = (B_window_end + next_unacked) % BUFFER_SIZE;
    }
    if(DEBUG)printf("acking up until %d\n",B_curr_seqno);
    num_acks++;
    tolayer3(B,ack_packet);

  }else{
    num_corrupted_recvd++;
    if(DEBUG)printf("checksum failed for corrupt packet %s\n",packet.payload);
  }
}

/* the following rouytine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void
B_init (void)
{
  
  if(DEBUG)printf("B INIT IS BEING CALLED\n");
  B_buffer = (struct pkt*) malloc(BUFFER_SIZE * sizeof(struct pkt));
  B_buffer_acks = (bool*) malloc(BUFFER_SIZE * sizeof(bool)); 
  B_inserted = 0; 
  int i;
  struct pkt dummy;
  dummy.seqnum = -1;
  dummy.acknum = -1;
  memcpy(dummy.payload,"-------------------\n",20);
  for(i =0; i < BUFFER_SIZE; i++){
   B_buffer[i] = dummy; 
   B_buffer[i].seqnum = i;
   B_buffer[i].acknum= i;
   B_buffer_acks[i] = 0;
  }


  B_curr_seqno = FIRST_SEQNO-1;
  B_next_buffer_index = 0;
  B_curr_acknum = FIRST_SEQNO;

  B_window_base = 0;
  B_window_end = WINDOW_SIZE-1;

  num_acks = 0;
  num_corrupted_recvd = 0;

} 

/* called at end of simulation to print final statistics */
void Simulation_done()
{
  double avg_rtt_calc = 0.0;
  int walk =0;
  printf("----------------------------STATS------------------------------\n");
  int i;
  for(i=0; i < A_next_buffer_index; i++){
    if(DEBUG)printf("%f | ",A_packet_timers[i]);
    avg_rtt_calc+= A_packet_timers[i];
    walk++;
  }
  printf("\n");
  avg_rtt_calc= avg_rtt_calc/(double)walk;
  printf("Original packets sent: %d\nNumer of Retransmission: %d\n,Number of Acks: %d\nNumber of corruptions:%d/%d\nNumberLost: %d\nAVG RTT: %f\n",num_original_packets
  ,num_retransmissions
  ,num_acks
  ,num_corrupted_recvd
  ,total_num_corrupted
  ,total_lost
  ,avg_rtt_calc);
  //double* avg_rtt;
}

/*****************************************************************
***************** NETWORK EMULATION CODE STARTS BELOW ***********
The code below emulates the layer 3 and below network environment:
  - emulates the tranmission and delivery (possibly with bit-level corruption
    and packet loss) of packets across the layer 3/4 interface
  - handles the starting/stopping of a timer, and generates timer
    interrupts (resulting in calling students timer handler).
  - generates message to be sent (passed from later 5 to 4)

THERE IS NOT REASON THAT ANY STUDENT SHOULD HAVE TO READ OR UNDERSTAND
THE CODE BELOW.  YOU SHOLD NOT TOUCH, OR REFERENCE (in your code) ANY
OF THE DATA STRUCTURES BELOW.  If you're interested in how I designed
the emulator, you're welcome to look at the code - but again, you should have
to, and you defeinitely should not have to modify
******************************************************************/


struct event {
   double evtime;           /* event time */
   int evtype;             /* event type code */
   int eventity;           /* entity where event occurs */
   struct pkt *pktptr;     /* ptr to packet (if any) assoc w/ this event */
   struct event *prev;
   struct event *next;
 };
struct event *evlist = NULL;   /* the event list */

/* Advance declarations. */
void init(void);
void generate_next_arrival(void);
void insertevent(struct event *p);


/* possible events: */
#define  TIMER_INTERRUPT 0
#define  FROM_LAYER5     1
#define  FROM_LAYER3     2

#define  OFF             0
#define  ON              1


int TRACE = 0;              /* for debugging purpose */
int fileoutput; 
double time_now = 0.000;
int WINDOW_SIZE;
int LIMIT_SEQNO;
double RXMT_TIMEOUT;
double lossprob;            /* probability that a packet is dropped  */
double corruptprob;         /* probability that one bit is packet is flipped */
double lambda;              /* arrival rate of messages from layer 5 */
int   ntolayer3;           /* number sent into layer 3 */
int   nlost;               /* number lost in media */
int ncorrupt;              /* number corrupted by media*/
int nsim = 0;
int nsimmax = 0;
unsigned int seed[5];         /* seed used in the pseudo-random generator */

int
main(int argc, char **argv)
{
   struct event *eventptr;
   struct msg  msg2give;
   struct pkt  pkt2give;

   int i,j;

   init();
   A_init();
   B_init();

   while (1) {
        eventptr = evlist;            /* get next event to simulate */
        if (eventptr==NULL)
           goto terminate;
        evlist = evlist->next;        /* remove this event from event list */
        if (evlist!=NULL)
           evlist->prev=NULL;
        if (TRACE>=2) {
           printf("\nEVENT time: %f,",eventptr->evtime);
           printf("  type: %d",eventptr->evtype);
           if (eventptr->evtype==0)
               printf(", timerinterrupt  ");
             else if (eventptr->evtype==1)
               printf(", fromlayer5 ");
             else
             printf(", fromlayer3 ");
           printf(" entity: %d\n",eventptr->eventity);
           }
        time_now = eventptr->evtime;    /* update time to next event time */
        if (eventptr->evtype == FROM_LAYER5 ) {
            generate_next_arrival();   /* set up future arrival */
            /* fill in msg to give with string of same letter */
	    j = nsim % 26;
	    for (i=0;i<20;i++)
	      msg2give.data[i]=97+j;
	    msg2give.data[19]='\n';
	    nsim++;
	    if (nsim==nsimmax+1)
	      break;
	    A_output(msg2give);
	}
          else if (eventptr->evtype ==  FROM_LAYER3) {
            pkt2give.seqnum = eventptr->pktptr->seqnum;
            pkt2give.acknum = eventptr->pktptr->acknum;
            pkt2give.checksum = eventptr->pktptr->checksum;
	    for (i=0;i<20;i++)
	      pkt2give.payload[i]=eventptr->pktptr->payload[i];
            if (eventptr->eventity ==A)      /* deliver packet by calling */
               A_input(pkt2give);            /* appropriate entity */
            else
               B_input(pkt2give);
            free(eventptr->pktptr);          /* free the memory for packet */
            }
          else if (eventptr->evtype ==  TIMER_INTERRUPT) {
	    A_timerinterrupt();
             }
          else  {
             printf("INTERNAL PANIC: unknown event type \n");
             }
        free(eventptr);
   }
terminate:
   Simulation_done(); /* allow students to output statistics */
   printf("Simulator terminated at time %.12f\n",time_now);
   return (0);
}


void
init(void)                         /* initialize the simulator */
{
  int i=0;
  printf("----- * Go Back N Network Simulator Version 1.1 * ------ \n\n");
  printf("Enter number of messages to simulate: ");
  scanf("%d",&nsimmax);
  printf("Enter packet loss probability [enter 0.0 for no loss]:");
  scanf("%lf",&lossprob);
  printf("Enter packet corruption probability [0.0 for no corruption]:");
  scanf("%lf",&corruptprob);
  printf("Enter average time between messages from sender's layer5 [ > 0.0]:");
  scanf("%lf",&lambda);
  printf("Enter window size [>0]:");
  scanf("%d",&WINDOW_SIZE);
  LIMIT_SEQNO = 2*WINDOW_SIZE;
  printf("Enter retransmission timeout [> 0.0]:");
  scanf("%lf",&RXMT_TIMEOUT);
  printf("Enter trace level:");
  scanf("%d",&TRACE);
  printf("Enter random seed: [>0]:");
  scanf("%d",&seed[0]);
  for (i=1;i<5;i++)
    seed[i]=seed[0]+i;
  fileoutput = open("OutputFile", O_CREAT|O_WRONLY|O_TRUNC,0644);
  if (fileoutput<0) 
    exit(1);
  ntolayer3 = 0;
  nlost = 0;
  ncorrupt = 0;
  time_now=0.0;                /* initialize time to 0.0 */
  generate_next_arrival();     /* initialize event list */
}

/****************************************************************************/
/* mrand(): return a double in range [0,1].  The routine below is used to */
/* isolate all random number generation in one location.  We assume that the*/
/* system-supplied rand() function return an int in therange [0,mmm]        */
/****************************************************************************/
int nextrand(int i)
{
  seed[i] = seed[i]*1103515245+12345;
  return (unsigned int)(seed[i]/65536)%32768;
}

double mrand(int i)
{
  double mmm = 32767;   /* largest int  - MACHINE DEPENDENT!!!!!!!!   */
  double x;                   /* individual students may need to change mmm */
  x = nextrand(i)/mmm;            /* x should be uniform in [0,1] */
  if (TRACE==0)
    printf("%.16f\n",x);
  return(x);
}


/********************* EVENT HANDLINE ROUTINES *******/
/*  The next set of routines handle the event list   */
/*****************************************************/
void
generate_next_arrival(void)
{
   double x,log(),ceil();
   struct event *evptr;

    
   if (TRACE>2)
       printf("          GENERATE NEXT ARRIVAL: creating new arrival\n");

   x = lambda*mrand(0)*2;  /* x is uniform on [0,2*lambda] */
                             /* having mean of lambda        */
   evptr = (struct event *)malloc(sizeof(struct event));
   evptr->evtime =  time_now + x;
   evptr->evtype =  FROM_LAYER5;
   evptr->eventity = A;
   insertevent(evptr);
}

void
insertevent(p)
   struct event *p;
{
   struct event *q,*qold;

   if (TRACE>2) {
      printf("            INSERTEVENT: time is %f\n",time_now);
      printf("            INSERTEVENT: future time will be %f\n",p->evtime);
      }
   q = evlist;     /* q points to header of list in which p struct inserted */
   if (q==NULL) {   /* list is empty */
        evlist=p;
        p->next=NULL;
        p->prev=NULL;
        }
     else {
        for (qold = q; q !=NULL && p->evtime > q->evtime; q=q->next)
              qold=q;
        if (q==NULL) {   /* end of list */
             qold->next = p;
             p->prev = qold;
             p->next = NULL;
             }
           else if (q==evlist) { /* front of list */
             p->next=evlist;
             p->prev=NULL;
             p->next->prev=p;
             evlist = p;
             }
           else {     /* middle of list */
             p->next=q;
             p->prev=q->prev;
             q->prev->next=p;
             q->prev=p;
             }
         }
}

void
printevlist(void)
{
  struct event *q;
  printf("--------------\nEvent List Follows:\n");
  for(q = evlist; q!=NULL; q=q->next) {
    printf("Event time: %f, type: %d entity: %d\n",q->evtime,q->evtype,q->eventity);
    }
  printf("--------------\n");
}



/********************** Student-callable ROUTINES ***********************/

/* called by students routine to cancel a previously-started timer */
void
stoptimer(AorB)
int AorB;  /* A or B is trying to stop timer */
{
 struct event *q /* ,*qold */;
 if (TRACE>2)
    printf("          STOP TIMER: stopping timer at %f\n",time_now);
/* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next)  */
 for (q=evlist; q!=NULL ; q = q->next)
    if ( (q->evtype==TIMER_INTERRUPT  && q->eventity==AorB) ) {
       /* remove this event */
       if (q->next==NULL && q->prev==NULL)
             evlist=NULL;         /* remove first and only event on list */
          else if (q->next==NULL) /* end of list - there is one in front */
             q->prev->next = NULL;
          else if (q==evlist) { /* front of list - there must be event after */
             q->next->prev=NULL;
             evlist = q->next;
             }
           else {     /* middle of list */
             q->next->prev = q->prev;
             q->prev->next =  q->next;
             }
       free(q);
       return;
     }
  printf("Warning: unable to cancel your timer. It wasn't running.\n");
}


void
starttimer(AorB,increment)
int AorB;  /* A or B is trying to stop timer */
double increment;
{

 struct event *q;
 struct event *evptr;

 if (TRACE>2)
    printf("          START TIMER: starting timer at %f\n",time_now);
 /* be nice: check to see if timer is already started, if so, then  warn */
/* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next)  */
   for (q=evlist; q!=NULL ; q = q->next)
    if ( (q->evtype==TIMER_INTERRUPT  && q->eventity==AorB) ) {
      printf("Warning: attempt to start a timer that is already started\n");
      return;
      }

/* create future event for when timer goes off */
   evptr = (struct event *)malloc(sizeof(struct event));
   evptr->evtime =  time_now + increment;
   evptr->evtype =  TIMER_INTERRUPT;
   evptr->eventity = AorB;
   insertevent(evptr);
}


/************************** TOLAYER3 ***************/
void
tolayer3(AorB,packet)
int AorB;  /* A or B is trying to stop timer */
struct pkt packet;
{
 struct pkt *mypktptr;
 struct event *evptr,*q;
 double lastime, x;
 int i;


 ntolayer3++;

 /* simulate losses: */
 if (mrand(1) < lossprob)  {
      nlost++;
      if (TRACE>0){
        total_lost++;
        printf("          TOLAYER3: packet being lost\n");

      }
      return;
    }

/* make a copy of the packet student just gave me since he/she may decide */
/* to do something with the packet after we return back to him/her */
 mypktptr = (struct pkt *)malloc(sizeof(struct pkt));
 mypktptr->seqnum = packet.seqnum;
 mypktptr->acknum = packet.acknum;
 mypktptr->checksum = packet.checksum;
 for (i=0;i<20;i++)
   mypktptr->payload[i]=packet.payload[i];
 if (TRACE>2)  {
   printf("          TOLAYER3: seq: %d, ack %d, check: %d ", mypktptr->seqnum,
          mypktptr->acknum,  mypktptr->checksum);
   }

/* create future event for arrival of packet at the other side */
  evptr = (struct event *)malloc(sizeof(struct event));
  evptr->evtype =  FROM_LAYER3;   /* packet will pop out from layer3 */
  evptr->eventity = (AorB+1) % 2; /* event occurs at other entity */
  evptr->pktptr = mypktptr;       /* save ptr to my copy of packet */
/* finally, compute the arrival time of packet at the other end.
   medium can not reorder, so make sure packet arrives between 1 and 10
   time units after the latest arrival time of packets
   currently in the medium on their way to the destination */
 lastime = time_now;
/* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next) */
 for (q=evlist; q!=NULL ; q = q->next)
    if ( (q->evtype==FROM_LAYER3  && q->eventity==evptr->eventity) )
      lastime = q->evtime;
 evptr->evtime =  lastime + 1 + 9*mrand(2);



 /* simulate corruption: */
 if (mrand(3) < corruptprob)  {
    ncorrupt++;
    if ( (x = mrand(4)) < 0.75)
       mypktptr->payload[0]='?';   /* corrupt payload */
      else if (x < 0.875)
       mypktptr->seqnum = 999999;
      else
       mypktptr->acknum = 999999;
    if (TRACE>0){
     total_num_corrupted++;   
      printf("          TOLAYER3: packet being corrupted\n");
    }
    }

  if (TRACE>2)
     printf("          TOLAYER3: scheduling arrival on other side\n");
  insertevent(evptr);
}

void
tolayer5(datasent)
  char datasent[20];
{
  write(fileoutput,datasent,20);
}

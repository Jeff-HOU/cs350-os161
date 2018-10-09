#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>
/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

typedef struct Vehicles
{
    Direction origin;
    Direction destination;
} Vehicle;
static struct lock* globalLock;
static struct cv* globalCV;
struct array* vehicles;
static bool right_turn(Vehicle *v);
static bool conflict(Vehicle *a, Vehicle *b);

bool right_turn(Vehicle *v) {
    KASSERT(v != NULL);
    if (((v->origin == west) && (v->destination == south)) ||
        ((v->origin == south) && (v->destination == east)) ||
        ((v->origin == east) && (v->destination == north)) ||
        ((v->origin == north) && (v->destination == west))) {
        return true;
    } else {
        return false;
    }
}

bool conflict(Vehicle *a, Vehicle *b){
    if(a -> origin == b -> origin){
        return false;
    }else if((a -> origin == b -> destination) && (a -> destination == b -> origin)){
        return false;
    }else if((a -> destination != b -> destination) && (right_turn(a) || right_turn(b))){
        return false;
    }else{
        return true;
    }
}

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
//static struct semaphore *intersectionSem;

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */

//  intersectionSem = sem_create("intersectionSem",1);
    kprintf("intersection_sync_init starts");
    globalLock = lock_create("globalLock");
    globalCV = cv_create("globalCV");
    vehicles = array_create();
    if (globalLock == NULL || globalCV == NULL || vehicles == NULL) {
        panic("could not create intersection semaphore, traffic_synch.c intersection_sync_init");
    }
    kprintf("intersection_sync_init finishes");
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
    KASSERT(globalLock != NULL); lock_destroy(globalLock);
    KASSERT(globalCV != NULL);   cv_destroy(globalCV);
    KASSERT(vehicles != NULL);   array_destroy(vehicles);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
//  (void)origin;  /* avoid compiler complaint about unused parameter */
//  (void)destination; /* avoid compiler complaint about unused parameter */
//  KASSERT(intersectionSem != NULL);
//  P(intersectionSem);
    KASSERT(globalLock != NULL);
    KASSERT(globalCV != NULL);
    KASSERT(vehicles != NULL);
    lock_acquire(globalLock);
    Vehicle *v = kmalloc(sizeof(Vehicle));
    v -> origin = origin;
    v -> destination = destination;
    bool wait = true;
    kprintf("intersection_before_entry 1");
    while (wait) {
        kprintf("intersection_before_entry 2");
        for (unsigned i = 0; i < array_num(vehicles); ++i) {
            if ((array_num(vehicles) != 0) && (conflict(v, array_get(vehicles, i)))) {
                cv_wait(globalCV, globalLock);
                break;
            }
            wait = false;
            break;
        }
    }
    kprintf("intersection_before_entry 3");
    array_add(vehicles, v, NULL);
    lock_release(globalLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
//  (void)origin;  /* avoid compiler complaint about unused parameter */
//  (void)destination; /* avoid compiler complaint about unused parameter */
//  KASSERT(intersectionSem != NULL);
//  V(intersectionSem);
    kprintf("intersection_after_exit 0");
    KASSERT(globalLock != NULL);
    KASSERT(globalCV != NULL);
    KASSERT(vehicles != NULL);
    lock_acquire(globalLock);
    kprintf("intersection_after_exit 1");
    for (unsigned i = 0; i < array_num(vehicles); ++i) {
        Vehicle *v = array_get(vehicles, i);
        if ((v -> origin == origin) && (v -> destination == destination)) {
            array_remove(vehicles, i);
            cv_broadcast(globalCV, globalLock);
            break;
        }
    }
    kprintf("intersection_after_exit 2");
    lock_release(globalLock);
}

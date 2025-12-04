// Andrew Chen & Dominic Nguyen
// 12/3/2025

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#define CAR_COUNT 8 // total cards
#define MUTEX_COUNT 25 // 24 potential collisions (1-24) index 0 unused

// struct for directions
typedef struct _directions {
    char dir_original;
    char dir_target;
} directions;

// struct for car info
typedef struct car_info {
    int cid;
    float arrival_time;
    directions dir;
} car_info;

// Mutexes for potential collisions
pthread_mutex_t collision_mutex[MUTEX_COUNT];

// Reader count for each zone per direction
pthread_mutex_t reader_mutex[MUTEX_COUNT];
int zone_reader_count[MUTEX_COUNT][4]; // [zone][direction]

// Global tracking
pthread_mutex_t head_of_line_mutex;
int arrival_index = 0;
int car_order[CAR_COUNT];
int car_direction[CAR_COUNT]; // Store direction index for each car
int car_started_crossing[CAR_COUNT];

double start_time;


double get_current_time() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec + now.tv_nsec / 1e9);
}

double get_time() {
    return get_current_time() - start_time;
}

void sleep_until(double target_time) {
    double current = get_current_time();
    double sleep_duration = (start_time + target_time) - current;
    if (sleep_duration > 0) {
        usleep((unsigned int)(sleep_duration * 1000000));
    }
}

int map_direction(char d) {
    switch(d) {
        case '^': return 0;
        case 'v': return 1;
        case '<': return 2;
        case '>': return 3;
        default: return -1;
    }
}

// All possible legal moves any car can make from their starting direction ==> time to cross 
int crossing_time(char from, char to) {
    if (from == '^' && to == '<') return 5; // left turn = 5 second
    if (from == '^' && to == '^') return 4; // straight = 4 seconds
    if (from == '^' && to == '>') return 3; // right turn = 3 seconds
    if (from == 'v' && to == '>') return 5;
    if (from == 'v' && to == 'v') return 4;
    if (from == 'v' && to == '<') return 3;
    if (from == '<' && to == 'v') return 5;
    if (from == '<' && to == '<') return 4;
    if (from == '<' && to == '^') return 3;
    if (from == '>' && to == '^') return 5;
    if (from == '>' && to == '>') return 4;
    if (from == '>' && to == 'v') return 3;
    return -1;
}

// Based on the ms paint intersection diagram, returns the critical zones for a given turn
void critical_zones(char from, char to, int* zones, int *count) {
    *count = 0;
    if (from == '^') {
        if (to == '<') { zones[(*count)++]=24; zones[(*count)++]=20; zones[(*count)++]=13; zones[(*count)++]=11; zones[(*count)++]=9; zones[(*count)++]=3; }
        else if (to == '^') { zones[(*count)++]=24; zones[(*count)++]=21; zones[(*count)++]=16; zones[(*count)++]=10; zones[(*count)++]=7; zones[(*count)++]=2; }
        else if (to == '>') { zones[(*count)++]=24; zones[(*count)++]=22; }
    }
    else if (from == 'v') {
        if (to == '>') { zones[(*count)++]=1; zones[(*count)++]=5; zones[(*count)++]=12; zones[(*count)++]=14; zones[(*count)++]=16; zones[(*count)++]=22; }
        else if (to == 'v') { zones[(*count)++]=1; zones[(*count)++]=4; zones[(*count)++]=9; zones[(*count)++]=15; zones[(*count)++]=18; zones[(*count)++]=23; }
        else if (to == '<') { zones[(*count)++]=1; zones[(*count)++]=3; }
    }
    else if (from == '<') {
        if (to == 'v') { zones[(*count)++]=8; zones[(*count)++]=10; zones[(*count)++]=14; zones[(*count)++]=13; zones[(*count)++]=19; zones[(*count)++]=23; }
        else if (to == '<') { zones[(*count)++]=8; zones[(*count)++]=7; zones[(*count)++]=6; zones[(*count)++]=5; zones[(*count)++]=4; zones[(*count)++]=3; }
        else if (to == '^') { zones[(*count)++]=8; zones[(*count)++]=2; }
    }
    else if (from == '>') {
        if (to == '^') { zones[(*count)++]=17; zones[(*count)++]=15; zones[(*count)++]=11; zones[(*count)++]=12; zones[(*count)++]=6; zones[(*count)++]=2; }
        else if (to == '>') { zones[(*count)++]=17; zones[(*count)++]=18; zones[(*count)++]=19; zones[(*count)++]=20; zones[(*count)++]=21; zones[(*count)++]=22; }
        else if (to == 'v') { zones[(*count)++]=17; zones[(*count)++]=23; }
    }
}

void *Car(void *arg) {
    car_info *car = (car_info *)arg;
    int arrival_position;
    int original_car_direction = map_direction(car->dir.dir_original);
    double stop_until_time = car->arrival_time + 2.0; // stop for 2 seconds after arrival

    // ----ArriveIntersection---- //
    sleep_until(car->arrival_time);
    printf("Time %.1f: Car %d (%c %c) arriving\n", car->arrival_time, car->cid, 
           car->dir.dir_original, car->dir.dir_target);
    
    // Record order of arrival and direction
    pthread_mutex_lock(&head_of_line_mutex);
    arrival_position = arrival_index++;
    car_order[car->cid - 1] = arrival_position;
    car_direction[car->cid - 1] = original_car_direction;
    pthread_mutex_unlock(&head_of_line_mutex);

    // Stop for 2 seconds
    sleep_until(stop_until_time);

    // Wait for all cars that arrived earlier to start crossing
    int wait_for_others = 1;
    while (wait_for_others) {
        pthread_mutex_lock(&head_of_line_mutex);
        wait_for_others = 0;
        
        for (int i = 0; i < CAR_COUNT; i++) {
            if (i == car->cid - 1) continue;
            
            if (car_order[i] >= 0 && car_order[i] < arrival_position) {
                if (car_started_crossing[i] == 0) {
                    wait_for_others = 1;
                    break;
                }
            }
        }
        
        pthread_mutex_unlock(&head_of_line_mutex);
    }

    // ----CrossIntersection---- //
    int zone_ids[MUTEX_COUNT];
    int num_zones = 0;
    critical_zones(car->dir.dir_original, car->dir.dir_target, zone_ids, &num_zones);

    // Sort zones to prevent deadlock
    for (int i = 0; i < num_zones - 1; i++) {
        for (int j = i + 1; j < num_zones; j++) {
            if (zone_ids[i] > zone_ids[j]) {
                int tmp = zone_ids[i];
                zone_ids[i] = zone_ids[j];
                zone_ids[j] = tmp;
            }
        }
    }

    // Lock zones using reader-writer pattern
    // First pass: increment counts and identify which zones need locking
    int zones_to_lock[MUTEX_COUNT];
    int num_to_lock = 0;
    
    for (int i = 0; i < num_zones; i++) {
        int zone = zone_ids[i];
        pthread_mutex_lock(&reader_mutex[zone]);
        
        if (zone_reader_count[zone][original_car_direction] == 0) {
            zones_to_lock[num_to_lock++] = zone;
        }
        zone_reader_count[zone][original_car_direction]++;
        
        pthread_mutex_unlock(&reader_mutex[zone]);
    }
    
    // Second pass: actually lock the collision mutexes
    for (int i = 0; i < num_to_lock; i++) {
        pthread_mutex_lock(&collision_mutex[zones_to_lock[i]]);
    }

    // Mark started crossing and record the crossing start time
    double cross_start_time = get_time();
    pthread_mutex_lock(&head_of_line_mutex);
    car_started_crossing[car->cid - 1] = 1;
    pthread_mutex_unlock(&head_of_line_mutex);

    printf("Time %.1f: Car %d (%c %c) crossing\n", cross_start_time, car->cid, 
           car->dir.dir_original, car->dir.dir_target);

    // Cross
    int cross_duration = crossing_time(car->dir.dir_original, car->dir.dir_target);
    double exit_time = cross_start_time + cross_duration;
    sleep_until(exit_time);

    // ----ExitIntersection---- //
    printf("Time %.1f: Car %d (%c %c) exiting\n", exit_time, car->cid, 
           car->dir.dir_original, car->dir.dir_target);

    // Release zones - only unlock the ones WE locked
    for (int i = 0; i < num_to_lock; i++) {
        int zone = zones_to_lock[i];
        pthread_mutex_lock(&reader_mutex[zone]);
        
        zone_reader_count[zone][original_car_direction]--;
        
        pthread_mutex_unlock(&reader_mutex[zone]);
        pthread_mutex_unlock(&collision_mutex[zone]);
    }

    // Decrement counts for zones we didn't lock
    for (int i = 0; i < num_zones; i++) {
        int zone = zone_ids[i];
        int already_handled = 0;
        for (int j = 0; j < num_to_lock; j++) {
            if (zones_to_lock[j] == zone) {
                already_handled = 1;
                break;
            }
        }
        
        if (!already_handled) {
            pthread_mutex_lock(&reader_mutex[zone]);
            zone_reader_count[zone][original_car_direction]--;
            pthread_mutex_unlock(&reader_mutex[zone]);
        }
    }

    free(car);
    pthread_exit(NULL);
}

car_info *get_car_data(int cid, float arrival, char orig, char targ) {
    car_info *car = malloc(sizeof(car_info));
    car->cid = cid;
    car->arrival_time = arrival;
    car->dir.dir_original = orig;
    car->dir.dir_target = targ;
    return car;
}

int main() {

    // Declare an array of threads of size car count (8)
    pthread_t threads[CAR_COUNT];

    pthread_mutex_init(&head_of_line_mutex, NULL); // initialize HoL lock
    
    // Initialize car data
    for (int i = 0; i < CAR_COUNT; i++) {
        car_order[i] = -1;
        car_direction[i] = -1;
        car_started_crossing[i] = 0;
    }
    
    // Initialize collision mutexes
    for (int i = 0; i < MUTEX_COUNT; i++) {
        pthread_mutex_init(&collision_mutex[i], NULL);
        pthread_mutex_init(&reader_mutex[i], NULL);
        for (int j = 0; j < 4; j++) {
            zone_reader_count[i][j] = 0;
        }
    }

    // Tracks simulation time
    struct timespec begin;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    start_time = (double)(begin.tv_sec + begin.tv_nsec / 1e9);

    // create car info
    car_info *cars[CAR_COUNT] = {
        get_car_data(1, 1.1, '^', '^'),
        get_car_data(2, 2.2, '^', '^'),
        get_car_data(3, 3.3, '^', '<'),
        get_car_data(4, 4.4, 'v', 'v'),
        get_car_data(5, 5.5, 'v', '>'),
        get_car_data(6, 6.6, '^', '^'),
        get_car_data(7, 7.7, '>', '^'),
        get_car_data(8, 8.8, '<', '^')
    };

    // create threads for each car
    for (int i = 0; i < CAR_COUNT; i++) {
        pthread_create(&threads[i], NULL, Car, cars[i]);
    }

    // wait for threads to be created
    for (int i = 0; i < CAR_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    // Cleanup
    pthread_mutex_destroy(&head_of_line_mutex);
    for (int i = 0; i < MUTEX_COUNT; i++) {
        pthread_mutex_destroy(&collision_mutex[i]);
        pthread_mutex_destroy(&reader_mutex[i]);
    }

    return 0;
}
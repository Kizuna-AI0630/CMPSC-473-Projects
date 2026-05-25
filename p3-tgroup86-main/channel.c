#include "channel.h"

// Creates a new channel with the provided size and returns it to the caller
// A 0 size indicates an unbuffered channel, whereas a positive size indicates a buffered channel

// Helper function to add a waiting thread's semaphore to the channel's waiting list
static void add_select_waiter(sem_t*** array, size_t* count, size_t* capacity, sem_t* sem) {
    if (*count == *capacity) {
        if (*capacity == 0) {
            *capacity = 4; // initial capacity
        } else {
            *capacity *= 2; // double the capacity
        }
        
        *array = (sem_t**)realloc(*array, *capacity * sizeof(sem_t*));
    }
    (*array)[(*count)++] = sem;
}

// Helper function to remove a waiting thread's semaphore from the channel's waiting list
static void remove_select_waiter(sem_t*** array, size_t* count, sem_t* sem) {
    for (size_t i = 0; i < *count; i++) {
        if ((*array)[i] == sem) {
            (*array)[i] = (*array)[*count - 1]; 
            (*count)--;
            break;
        }
    }
}

chan_t* channel_create(size_t size)
{
    /* IMPLEMENT THIS */
    // 1. allocate memory for the channel
    chan_t* channel = (chan_t*)malloc(sizeof(chan_t));

    // 2. initialize the buffer with the given size
    (*channel).buffer = buffer_create(size);

    // 3. initialize the mutex lock
    pthread_mutex_init(&(*channel).mutex, NULL);

    // 4. initialize the condition variables
    pthread_cond_init(&(*channel).send_cv, NULL);
    pthread_cond_init(&(*channel).receive_cv, NULL);

    // 5. initialize the is_closed variable to false
    (*channel).is_closed = false;

    // 6. initialize the send_waiting and receive_waiting arrays and their counts and capacities
    // Only when select is used, these arrays are used.
    (*channel).receive_waiting = NULL;
    (*channel).receive_waiting_count = 0;
    (*channel).receive_waiting_capacity = 0;
    
    (*channel).send_waiting = NULL;
    (*channel).send_waiting_count = 0;
    (*channel).send_waiting_capacity = 0;
    
    return channel;
}

// Writes data to the given channel
// This can be both a blocking call i.e., the function only returns on a successful completion of send (blocking = true), and
// a non-blocking call i.e., the function simply returns if the channel is full (blocking = false)
// In case of the blocking call when the channel is full, the function waits till the channel has space to write the new data
// Returns SUCCESS for successfully writing data to the channel,
// WOULDBLOCK if the channel is full and the data was not added to the buffer (non-blocking calls only),
// CLOSED_ERROR if the channel is closed, and
// OTHER_ERROR on encountering any other generic error of any sort
enum chan_status channel_send(chan_t* channel, void* data, bool blocking)
{
    /* IMPLEMENT THIS */

    //1. lock the mutex
    pthread_mutex_lock(&(*channel).mutex);

    //2. If the channel is closed, return error
    if ((*channel).is_closed) {
        pthread_mutex_unlock(&(*channel).mutex);
        return CLOSED_ERROR;
    }
    
    //3. If the channel is full and blocking is true, wait on the send_cv
    if (blocking) {
        // Wait until there is space in the buffer to add the new data
        while (buffer_current_size((*channel).buffer) == buffer_capacity((*channel).buffer)) {
            pthread_cond_wait(&(*channel).send_cv, &(*channel).mutex);
            
            // If the channel is closed while waiting, return error
            if ((*channel).is_closed) {
                pthread_mutex_unlock(&(*channel).mutex);
                return CLOSED_ERROR;
            }
        }
    } else {
        // If not blocking and the channel is full, return WOULDBLOCK
        if (buffer_current_size((*channel).buffer) == buffer_capacity((*channel).buffer)) {
            pthread_mutex_unlock(&(*channel).mutex);
            return WOULDBLOCK;
        }
    }
    
    //5. Add the data to the buffer
    buffer_add(data, (*channel).buffer);

    //6. Wake the waiting receivers by signaling the receive_cv
    pthread_cond_signal(&(*channel).receive_cv);
    for (size_t i = 0; i < (*channel).receive_waiting_count; i++) {
        sem_post((*channel).receive_waiting[i]);
    }

    //7. unlock the mutex
    pthread_mutex_unlock(&(*channel).mutex);
    return SUCCESS;
}

// Reads data from the given channel and stores it in the function’s input parameter, data (Note that it is a double pointer).
// This can be both a blocking call i.e., the function only returns on a successful completion of receive (blocking = true), and
// a non-blocking call i.e., the function simply returns if the channel is empty (blocking = false)
// In case of the blocking call when the channel is empty, the function waits till the channel has some data to read
// Returns SUCCESS for successful retrieval of data,
// WOULDBLOCK if the channel is empty and nothing was stored in data (non-blocking calls only),
// CLOSED_ERROR if the channel is closed, and
// OTHER_ERROR on encountering any other generic error of any sort
enum chan_status channel_receive(chan_t* channel, void** data, bool blocking)
{
    /* IMPLEMENT THIS */
    // Similar to channel_send, but in reverse order for receive

    //1. lock the mutex
    pthread_mutex_lock(&(*channel).mutex);

    //2. If the channel is closed, return error
    if ((*channel).is_closed) {
        pthread_mutex_unlock(&(*channel).mutex);
        return CLOSED_ERROR;
    }
    
    //3. If the buffer is empty and blocking is true, wait on the receive_cv
    if (blocking) {
        // Wait until there is some data in the buffer to read
        while (buffer_current_size((*channel).buffer) == 0) {
            pthread_cond_wait(&(*channel).receive_cv, &(*channel).mutex);
            
            // If the channel is closed while waiting, return error
            if ((*channel).is_closed) {
                pthread_mutex_unlock(&(*channel).mutex);
                return CLOSED_ERROR;
            }
        }
    } else {
        // If not blocking and the buffer is empty, return WOULDBLOCK
        if (buffer_current_size((*channel).buffer) == 0) {
            pthread_mutex_unlock(&(*channel).mutex);
            return WOULDBLOCK;
        }
    }
    
    //5. Retrieve the data from the buffer and store it in the input parameter, data
    *data = buffer_remove((*channel).buffer);

    //6. Wake the waiting receivers by signaling the receive_cv
    pthread_cond_signal(&(*channel).send_cv);
    for (size_t i = 0; i < (*channel).send_waiting_count; i++) {
        sem_post((*channel).send_waiting[i]);
    }

    //7. unlock the mutex
    pthread_mutex_unlock(&(*channel).mutex);
    return SUCCESS;
}

// Closes the channel and informs all the blocking send/receive/select calls to return with CLOSED_ERROR
// Once the channel is closed, send/receive/select operations will cease to function and just return CLOSED_ERROR
// Returns SUCCESS if close is successful,
// CLOSED_ERROR if the channel is already closed, and
// OTHER_ERROR in any other error case
enum chan_status channel_close(chan_t* channel)
{
    /* IMPLEMENT THIS */
    // 1. check if the channel is closed, if not return CLOSED_ERROR
    pthread_mutex_lock(&(*channel).mutex);
    if ((*channel).is_closed) {
        pthread_mutex_unlock(&(*channel).mutex);
        return CLOSED_ERROR;
    }

    // 2. Close out the channel
    (*channel).is_closed = true;

    //3. Wake all the waiting senders and receivers and let them know that the channel is closed
    pthread_cond_broadcast(&(*channel).receive_cv);
    pthread_cond_broadcast(&(*channel).send_cv);

    // For select, we also perform the same for the waiting senders and receivers
    for (size_t i = 0; i < (*channel).send_waiting_count; i++) {
        sem_post((*channel).send_waiting[i]);
    }
    for (size_t i = 0; i < (*channel).receive_waiting_count; i++) {
        sem_post((*channel).receive_waiting[i]);
    }

    // 4. unlock the mutex
    pthread_mutex_unlock(&(*channel).mutex);

    return SUCCESS;
}

// Frees all the memory allocated to the channel
// The caller is responsible for calling channel_close and waiting for all threads to finish their tasks before calling channel_destroy
// Returns SUCCESS if destroy is successful,
// DESTROY_ERROR if channel_destroy is called on an open channel, and
// OTHER_ERROR in any other error case
enum chan_status channel_destroy(chan_t* channel)
{
    /* IMPLEMENT THIS */
    // 1. check if the channel is closed, if not return DESTROY_ERROR
    pthread_mutex_lock(&(*channel).mutex);
    if (!(*channel).is_closed) {
        pthread_mutex_unlock(&(*channel).mutex);
        return DESTROY_ERROR;
    }

    pthread_mutex_unlock(&(*channel).mutex); // unlock the mutex before destroying the channel

    // 2. destroy the mutex and condition variables
    pthread_mutex_destroy(&(*channel).mutex);
    pthread_cond_destroy(&(*channel).send_cv);
    pthread_cond_destroy(&(*channel).receive_cv);

    // 3. free the channel memory
    free((*channel).receive_waiting);
    free((*channel).send_waiting);

    // 4. free the buffer
    buffer_free((*channel).buffer);

    // 5. free the channel memory
    free(channel);

    return SUCCESS;
}

// Takes an array of channels, channel_list, of type select_t and the array length, channel_count, as inputs
// This API iterates over the provided list and finds the set of possible channels which can be used to invoke the required operation (send or receive) specified in select_t
// If multiple options are available, it selects the first option and performs its corresponding action
// If no channel is available, the call is blocked and waits till it finds a channel which supports its required operation
// Once an operation has been successfully performed, select should set selected_index to the index of the channel that performed the operation and then return SUCCESS
// In the event that a channel is closed or encounters any error, the error should be propagated and returned through select
// Additionally, selected_index is set to the index of the channel that generated the error
enum chan_status channel_select(size_t channel_count, select_t* channel_list, size_t* selected_index)
{
    /* IMPLEMENT THIS */

    // 1. Set a semphore to wake up the select call when any of the channels in the channel_list has activity.
    sem_t select_sem;
    sem_init(&select_sem, 0, 0);

    // 2. Register the select call on all the channels in the channel_list by adding the select_sem to their waiting lists
    for (size_t i = 0; i < channel_count; i++) {
        chan_t* channel = channel_list[i].channel;
        
        // Lock Channel mutex before modifying the waiting lists
        pthread_mutex_lock(&(*channel).mutex);

        // If sending, add to the send_waiting list, else add to the receive_waiting list
        if (channel_list[i].is_send) {
            add_select_waiter(&(*channel).send_waiting, &(*channel).send_waiting_count, &(*channel).send_waiting_capacity, &select_sem);
        } else {
            add_select_waiter(&(*channel).receive_waiting, &(*channel).receive_waiting_count, &(*channel).receive_waiting_capacity, &select_sem);
        }
        pthread_mutex_unlock(&(*channel).mutex);
    }

    // Wait for a channel to be ready
    enum chan_status status = SUCCESS;
    bool completed = false;

    // 3. Program keep checking if any of the channels in the channel_list can perform the required operation specified in select_t
    while (!completed) {
        // 1. Check if any channel is ready to perform actions.
        for (size_t i = 0; i < channel_count; i++) {
            chan_t* channel = channel_list[i].channel;
            
            pthread_mutex_lock(&(*channel).mutex);
            
            // If the channel is closed, return error
            if ((*channel).is_closed) {
                *selected_index = i;
                status = CLOSED_ERROR;
                completed = true;
                pthread_mutex_unlock(&(*channel).mutex);
                break;
            }
            
            // If the channel is not closed, try send
            if (channel_list[i].is_send) {
                // if buffer not full and we can send
                if (buffer_current_size((*channel).buffer) < buffer_capacity((*channel).buffer)) {
                    // 1. Store the data to be sent in the channel's buffer
                    buffer_add(channel_list[i].data, (*channel).buffer);
                    
                    // 2. Wake the waiting receivers by signaling the receive_cv
                    pthread_cond_signal(&(*channel).receive_cv);
                    for (size_t j = 0; j < (*channel).receive_waiting_count; j++) {
                        sem_post((*channel).receive_waiting[j]);
                    }
                    
                    // 3. Record the op and exit the loop
                    *selected_index = i;
                    status = SUCCESS;
                    completed = true;
                    pthread_mutex_unlock(&(*channel).mutex);
                    break;
                }
            } 
            // If the channel is not closed, try receive
            else {
                if (buffer_current_size((*channel).buffer) > 0) {
                    channel_list[i].data = buffer_remove((*channel).buffer);
                    
                    pthread_cond_signal(&(*channel).send_cv);
                    for (size_t j = 0; j < (*channel).send_waiting_count; j++) {
                        sem_post((*channel).send_waiting[j]);
                    }
                    
                    *selected_index = i;
                    status = SUCCESS;
                    completed = true;
                    pthread_mutex_unlock(&(*channel).mutex);
                    break;
                }
            }
            
            pthread_mutex_unlock(&(*channel).mutex);
        }

        // If all channl chekced, end chekcing loop
        if (completed) break;

        // If no channel is ready, wait to be signaled by one of the channels when they have some activity
        sem_wait(&select_sem);
    }

    // 4. Remove all the select_sem from the waiting lists of all the channels in the channel_list, prepare for the next select call
    for (size_t i = 0; i < channel_count; i++) {
        chan_t* channel = channel_list[i].channel;
        
        pthread_mutex_lock(&(*channel).mutex);
        if (channel_list[i].is_send) {
            remove_select_waiter(&(*channel).send_waiting, &(*channel).send_waiting_count, &select_sem);
        } else {
            remove_select_waiter(&(*channel).receive_waiting, &(*channel).receive_waiting_count, &select_sem);
        }
        pthread_mutex_unlock(&(*channel).mutex);
    }

    // Destroy the semaphore, preventing memory leak
    sem_destroy(&select_sem);
    return status;
}

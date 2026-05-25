# Final Submission Work

## Function Modified
1. channel create
    - Allocate an empty channel
    - Initalize the buffer space/size
    - Initialize the mutex lock
    - Initialize the condition variable which is used to wait for the channel to be ready
    - Initialize the condition variable which is used to wait for the channel to be closed
    - initalize a waitlist and ready to record the senders.

2. channel destroy
    - Comfirm the channel is closed
    - Destroy the lock
    - Destroy the list
    - Return everything

3. channel send
    - Get the key.
    - Check if channel is close, if so walk away and return error. 
    - Check if buffer is full and block is true, if so then wait until empty.
    - If Block is false, drop the key and return later.
    - Add the data to the buffer.
    - Wake up the waiting receivers to let them know there is food.
    - Drop the key and return success.

4. channel receive
    - Get the key.
    - Check if channel is close, if so walk away and return error.
    - Check if buffer is empty and block is true, if so then wait until data is available.
    - If Block is false and buffer is empty, drop the key and return later.
    - Take the data from the buffer.
    - Wake up the waiting senders to let them know there is empty space.
    - Drop the key and return success.

5. channel close
    - Get the key.
    - Check if the channel is already closed, if so walk away and return error.
    - Mark the channel as permanently closed.
    - Broadcast and and wake up all the sleeping senders and receivers.
    - Using sem_post to alert all the select waiters to let them know channel is closed.
    - Drop the key.

6. channel select
    - Create local semaphore for the current thread.
    - Register the semaphore by leaving its pointer in the waitlist of every channel we are watching.
    - Loop through all channels to see if any is ready to send or receive.
    - If none are ready, hold the semaphore and go to sleep until someone rings it.
    - Once a channel is successfully processed or closed, break the loop.
    - Clean up by erasing the semaphore pointer from every channel's waitlist before leaving.
    - Destroy the semaphore to prevent memory leaks.

Helper functions:
1. add_select_waiter
    - Adding a semaphore to be added to a channel's waitlist.
    - Check if the list capacity is full, if so, double its size to get a bigger waitlit.
    - Put the new semaphore at the end of the array list.

2. remove_select_waiter
    - Find the specific waiter in the array.
    - Copy the very last element in the array and overwrite the target so the list don't have to perform delete and move forward operations.
    - Shrink the array count by 1.
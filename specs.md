# Specifications
This programming exercise will assess your understanding of process synchronization.
Consider the following synchronization problem:
Suppose you are tasked to create a solution that will manage the LFG (Looking for Group) dungeon queuing of an MMORPG.

- a) There are only n instances that can be concurrently active. Thus, there can only be a maximum n number of parties that are currently in a dungeon.
- b) A standard party of 5 is 1 tank, 1 healer, 3 DPS.
- c) The solution should not result in a deadlock.
- d) The solution should not result in starvation.
- e) It is assumed that the input values arrived at the same time.
- f) A time value (in seconds) t is randomly selected between t1 and t2. Where t1 represents the fastest clear time of a dungeon instance and t2 is the slowest clear time of a dungeon instance. For ease of testing t2 <= 15.

## Input
The program accepts three inputs from the user.

n - maximum number of concurrent instances

t - number of tank players in the queue

h - number of healer players in the queue

d - number of DPS players in the queue

t1 - minimum time before an instance is finished

t2 - maximum time before an instance is finished 

## Output
The output of the program should show the following:
Current status of all available instances
If there is a party in the instance, the status should say "active"
If the instance is empty, the status should say "empty"
At the end of the execution there should be a summary of how many parties an instance have served and total time served.
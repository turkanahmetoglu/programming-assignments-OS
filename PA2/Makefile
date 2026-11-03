CC = gcc
LDFLAGS = -lpthread
all: sim

sim: sim_methods.c multi_sched.c multi_sched.h
	$(CC) sim_methods.c multi_sched.c $(LDFLAGS) -o sim

clean:
	rm -f sim *.o
/*
 * mtdev2tuio
 *
 * Copyright (C) 2011 Paolo Olivo <olivopaolo@tiscali.it>
 *
 * This tool is based on the excellent work of:
 * mtdev2tuio
 *      
 * liblo
 * 	Copyright (C) 2004 Steve Harris, Uwe Koloska (LGPL)
 *
 * mtdev - Multitouch Protocol Translation Library (MIT license)
 * 	Copyright (C) 2010 Henrik Rydberg <rydberg@euromail.se>
 * 	Copyright (C) 2010 Canonical Ltd.
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************/

/* TODO:
   - calculate acceleration
*/
#include "lo/lo.h"
#include <mtdev.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <sys/utsname.h>
#include <sys/file.h>

#define NSEC_PER_USEC   1000L
#define NSEC_PER_SEC    1000000000L

typedef __u64 nstime;

static inline __u64 timeval_to_ns(const struct timeval *tv) {
  return ((__u64) tv->tv_sec * NSEC_PER_SEC) + tv->tv_usec * NSEC_PER_USEC ;
}

static float calc_speed(float s, float s_1, nstime t, nstime t_1) {
  return ((s - s_1) * (float)NSEC_PER_SEC / (t - t_1)) ;
}

struct slot_t {
  /** session id */
  int s_id ;
  /** relative position */
  float x, y ;
  /** relative speed */
  float X, Y ;
  /** previous position */
  float x_1, y_1 ;
  /** previous sample time */
  nstime t_x, t_y ;
  int changed ;
} ;

struct state_t {
  /** frame id */
  unsigned int f_id ;
  /** address of out tuio client*/
  lo_address tuioaddr ;
  /** number of slots */
  unsigned int maxslots ;
  /** current slot */
  int cs ;		
  /** slots information */
  struct slot_t *slot ;
} ;

struct device_t {
  /** device filepath */
  char *filepath ;
  /** file descriptor */
  int fd ;
  /** mtdev device structure */
  struct mtdev dev ;
  /** normalisation values */
  int x_ofs, y_ofs;
  float x_scale, y_scale;
} ;

static void send_tuio(struct state_t *s, struct device_t *d) {
  lo_bundle  bundle ;
  lo_message msg_source ;
  lo_message msg_alive ;
  lo_message msg_fseq ;
  lo_timetag timetag ;
  lo_timetag_now(&timetag) ;
  bundle = lo_bundle_new(timetag) ;
  // create source message
  msg_source = lo_message_new() ;
  struct utsname info;
  uname(&info) ;
  char buff[128] ;
  sprintf(buff, "mtdev2tuio-%s@%s",d->filepath, info.nodename) ;
  lo_message_add_string(msg_source, "source");
  lo_message_add_string(msg_source, buff);
  lo_bundle_add_message(bundle, "/tuio/2Dcur", msg_source) ;
  // create alive message
  msg_alive = lo_message_new() ;
  lo_message_add_string(msg_alive, "alive");
  unsigned int i; for (i = 0; i < s->maxslots; i++) {
    if (s->slot[i].s_id != -1)
      lo_message_add_int32(msg_alive, s->slot[i].s_id);    
  }
  lo_bundle_add_message(bundle, "/tuio/2Dcur", msg_alive) ;
  // create set messages
  for (i = 0; i < s->maxslots; i++) {
    if (s->slot[i].s_id != -1 && s->slot[i].changed) {
      s->slot[i].changed = 0 ;
      lo_message msg_set ;
      msg_set = lo_message_new() ;
      lo_message_add_string(msg_set, "set");
      lo_message_add_int32(msg_set, s->slot[i].s_id) ;
      lo_message_add_float(msg_set, s->slot[i].x) ;
      lo_message_add_float(msg_set, s->slot[i].y) ;
      lo_message_add_float(msg_set, s->slot[i].X) ;
      lo_message_add_float(msg_set, s->slot[i].Y) ;
      lo_message_add_float(msg_set, 0.0) ;
      lo_bundle_add_message(bundle, "/tuio/2Dcur", msg_set) ;
    }
  }
  // create fseq message
  msg_fseq = lo_message_new() ;
  lo_message_add_string(msg_fseq, "fseq") ;
  lo_message_add_int32(msg_fseq, s->f_id) ;
  lo_bundle_add_message(bundle, "/tuio/2Dcur", msg_fseq) ;
  // sent bundle
  if (lo_send_bundle(s->tuioaddr, bundle) == -1)
      printf("OSC error %d: %s\n", lo_address_errno(s->tuioaddr), lo_address_errstr(s->tuioaddr));
  lo_bundle_free(bundle) ;
}

static void process_event(struct state_t *s, struct device_t *d, const struct input_event *ev) {
  nstime time ;
  if (ev->type == EV_ABS) {
    if (ev->code == ABS_MT_SLOT) 
      s->cs = ev->value ; 
    else if (s->cs < s->maxslots) {
      switch (ev->code) {
      case ABS_MT_TRACKING_ID:
	s->slot[s->cs].s_id = ev->value ;
	break ;
      case ABS_MT_POSITION_X:
	s->slot[s->cs].x_1 = s->slot[s->cs].x ;
	s->slot[s->cs].x = (ev->value - d->x_ofs) * d->x_scale ;
	time = timeval_to_ns(&ev->time) ;
	s->slot[s->cs].X = calc_speed(s->slot[s->cs].x, s->slot[s->cs].x_1, time, s->slot[s->cs].t_x) ;
	s->slot[s->cs].t_x = time ;
	// this slot has been changed
	s->slot[s->cs].changed = 1 ;
	break;
      case ABS_MT_POSITION_Y :
	s->slot[s->cs].y_1 = s->slot[s->cs].y ;
	s->slot[s->cs].y = (ev->value - d->y_ofs) * d->y_scale ;
	time = timeval_to_ns(&ev->time) ;
	s->slot[s->cs].Y = calc_speed(s->slot[s->cs].y, s->slot[s->cs].y_1, time, s->slot[s->cs].t_y) ;
	s->slot[s->cs].t_y = time ;
	// this slot has been changed
	s->slot[s->cs].changed = 1 ;
	break;
      default: break;
      }
    }
  } else if (ev->type == EV_SYN && ev->code == SYN_REPORT) {    
    if (s->f_id > 0) send_tuio(s, d) ;
    ++s->f_id ;
  }
}

struct device_t device;

void terminate (int param) {
  fprintf (stderr, "\nReleasing device...\n") ;
  mtdev_close(&device.dev);
  flock(device.fd, LOCK_UN);
  close(device.fd);
  exit(0) ;
}

int main(int argc, char *argv[]) {
  struct state_t state ;
  struct input_event ev ;
  static struct option long_opt[] =
    { {"help", no_argument, 0, 'h'},
      {"lock", no_argument, 0, 'l'},
      {0, 0, 0, 0}
    } ;
  int c = 0, opt = 0, lock = 0;
  // --- get command line options ---
  while (c != -1) {
    c = getopt_long(argc, argv, "hl", long_opt, &opt) ;
    switch (c) {
    case 'h' :
      fprintf(stderr, "Usage: sudo ./mtdev2tuio [OPTS]... DEVICE [osc.udp://ADDRESS:PORT]\n") ;
      fprintf(stderr, "OPTIONS\n\
        -l | --lock\n\
               Require an exclusive lock to grab the device (using flock(2)).\n\
        -h | --help\n\
               Show help.\n\
") ;
      return 0 ;
    case 'l' : 
      lock = 1 ; break ;
    default : break ;
    }
  }
  if (optind >= argc) {
    fprintf(stderr, "Usage: sudo ./mtdev2tuio [OPTS]... DEVICE [osc.udp://ADDRESS:PORT]\n") ;
    return -1;
  }
  // --- initialize device ---
  device.filepath = argv[optind++] ;
  device.fd = open(device.filepath, O_RDONLY | O_NONBLOCK);
  if (device.fd < 0) {
    fprintf(stderr, "error: could not open device\n");
    return -1;
  }
  if (flock(device.fd, (lock?LOCK_EX:LOCK_SH) | LOCK_NB)) {
    fprintf(stderr, "error: could not grab the device\n");
    return -1;
  }
  if (mtdev_open(&device.dev, device.fd)) {  
    fprintf(stderr, "error: could not open device!\n") ;
    return -1 ;
  }
  // get device values
  device.x_ofs = mtdev_get_abs_minimum(&device.dev, ABS_MT_POSITION_X) ;
  device.y_ofs = mtdev_get_abs_minimum(&device.dev, ABS_MT_POSITION_Y) ;
  device.x_scale = 1.0f / (mtdev_get_abs_maximum(&device.dev, ABS_MT_POSITION_X) - device.x_ofs) ;
  device.y_scale = 1.0f / (mtdev_get_abs_maximum(&device.dev, ABS_MT_POSITION_Y) - device.y_ofs) ;

  // --- initialize state ---
  if (mtdev_has_mt_event(&device.dev, ABS_MT_SLOT))
    state.maxslots = mtdev_get_abs_maximum(&device.dev, ABS_MT_SLOT) ;
  else
    state.maxslots = 8 ;
  struct slot_t slot[state.maxslots] ;  
  state.slot = slot ;
  // mark all slots as unused
  int i; for (i = 0; i < state.maxslots; i++) {
    state.slot[i].s_id = -1 ;
    state.slot[i].changed = 0 ;
  }
  state.cs = 0 ;
  state.f_id = 0 ;
  // set the tuio address
  if (optind < argc) 
    state.tuioaddr = lo_address_new_from_url(argv[optind++]);
  else
    state.tuioaddr = lo_address_new(NULL, "3333");

  fprintf(stderr, "Sending OSC/TUIO packets to %s", lo_address_get_url(state.tuioaddr)) ;
  signal (SIGINT,terminate);
  signal (SIGTERM,terminate);

  // process all available events
  while (1) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(device.fd, &rfds);

    int retval = select(device.fd+1, &rfds, NULL, NULL, NULL);

    if(retval<0) {
        perror("select");
        return 1;
    }

    if(FD_ISSET(device.fd, &rfds)) {
        while (mtdev_get(&device.dev, device.fd, &ev, 1) > 0) {
          process_event(&state, &device, &ev);
        }
    }

  }
  return 0;
}

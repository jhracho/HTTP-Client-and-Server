#!/usr/bin/env python3

import concurrent.futures
import os
import requests
import sys
import time

# Functions

def usage(status=0):
    progname = os.path.basename(sys.argv[0])
    print(f'''Usage: {progname} [-h HAMMERS -t THROWS] URL
    -h  HAMMERS     Number of hammers to utilize (1)
    -t  THROWS      Number of throws per hammer  (1)
    -v              Display verbose output
    ''')
    sys.exit(status)

def hammer(url, throws, verbose, hid):
    ''' Hammer specified url by making multiple throws (ie. HTTP requests).

    - url:      URL to request
    - throws:   How many times to make the request
    - verbose:  Whether or not to display the text of the response
    - hid:      Unique hammer identifier

    Return the average elapsed time of all the throws.
    '''
    # Declaring elapsed time list for avg calculation
    elapsedTimes = []
    
    # for loop for individual throws
    for i in range(throws):
        
        # HTTP Request
        tSStart = time.time()
        r = requests.get(url);
        tSEnd = time.time()

        if (verbose):
            print(r.text)

        tSElapsed = tSEnd - tSStart
        elapsedTimes.append(tSElapsed)
        print(f'Hammer: {hid}, Throw:   {i}, Elapsed Time: {tSElapsed:0.2f}')

    # Returns average elapsed time for a particular hammer

    tSAVG = sum(elapsedTimes) / len(elapsedTimes)
    print(f'Hammer: {hid}, AVERAGE   , Elapsed Time: {tSAVG:0.2f}')

    return tSAVG;

def do_hammer(args):
    ''' Use args tuple to call `hammer` '''
    return hammer(*args)

def main():
    hammers = 1
    throws  = 1
    verbose = False
    setURL  = False
    url     = ''

    # Parse command line arguments
    arguments = sys.argv[1:]
    skip = False
    index = 1

    if len(arguments) == 0:                     # If no arguments
        usage(1)

    for arg in arguments:
        if skip:
            skip = False
        elif arg == '-h':                       # Hammers
            if int(arguments[index]):
                hammers = int(arguments[index])
                skip = True
        elif arg == '-t':                       # Throws
            if int(arguments[index]):
                throws = int(arguments[index])
                skip = True
        elif arg == '-v':                       # Verbose flag
            verbose = True
        elif arg[0] == '-' and len(arg) == 2:   # Prevents unknown flags
            usage(1)
        else:
            url = arg
            setURL = True
            break
        index += 1

    if not setURL:                              # If no URL
        usage(1)

    # Create pool of workers and perform throws
    avgTimes = []
    args = ((url, throws, verbose, hid) for hid in range(hammers))
    with concurrent.futures.ProcessPoolExecutor(hammers) as executor:
        avgElapsed = list(executor.map(do_hammer, args))
        
    print(f'TOTAL AVERAGE ELAPSED TIME: {sum(avgElapsed)/len(avgElapsed):0.2f}')
    

# Main execution

if __name__ == '__main__':
    main()

# vim: set sts=4 sw=4 ts=8 expandtab ft=python:

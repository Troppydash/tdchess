#!/bin/bash

DYLD_INSERT_LIBRARIES=$(brew --prefix jemalloc)/lib/libjemalloc.dylib MALLOC_CONF="background_thread:true,metadata_thp:auto,dirty_decay_ms:30000" ../builds/1.9.6/tdchess

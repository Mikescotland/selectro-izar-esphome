# Licensing and provenance

The standalone `izar_decoder.cpp` is an original adaptation of the known PRIOS LFSR transformation and status bit interpretation from wmbusmeters `driver_izar.cc`, `manufacturer_specificities.cpp`, and `izarv2.xmq`, which are GPL-3.0-or-later. That adapted implementation should also be treated as GPL-3.0-or-later. This is not a clean-room independent derivation and no proprietary licensing is implied.

`wmbus_radio` and `wmbus_common` are external dependencies to be fetched from SzczepanLeon's project; their respective licenses must also be complied with. Before selling or distributing firmware, complete an open-source license audit and provide the source and licensing notices required by the applicable licenses. The sources and linked repositories provide their definitive licensing terms.

This package does not copy the complete izarv2.xmq runtime or claim to implement its full specification.

Claude prototype scanner/frame code was consulted as a reference; it was not copied verbatim into this component. The reference archive and its own licensing must be reviewed before reusing any additional source.

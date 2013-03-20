IBR-DTN - A modular and lightweight implementation of the bundle protocol.
==========================================================================

This implementation of the bundle protocol RFC5050 is designed for embedded
systems like the RouterBoard 532A or Ubiquiti RouterStation Pro and can be
used as framework for DTN applications.

The module-based architecture with miscellaneous interfaces, makes it possible
to change functionalities like routing or storage of bundle just by inheriting
a specific class.

 * Bundle Protocol (RFC5050)
 * Bundle Security Protocol (draft-irtf-dtnrg-bundle-security)
 * Socket based API
 * AgeBlock support and bundle age tracking (draft-irtf-dtnrg-bundle-age-block-01)
 * Scope Control Hop Limit Block support
 * Experimental support for compressed bundle payload
 * Bundle-in-Bundle support
 * IPv6 support
 * Applications: dtnsend, dtnrecv, dtntrigger, dtnping, dtntracepath, dtninbox, dtnoutbox, dtnstream

 Convergence Layer
  * TCP/IP convergence layer - draft-irtf-dtnrg-tcp-clayer-02
  * TLS extension for TCP convergence layer by Stephen Röttger
  * UDP/IP convergence layer - draft-irtf-dtnrg-udp-clayer-00
  * IP neighbor discovery based on draft-irtf-dtnrg-ipnd-01
  * HTTP convergence layer by Robert Heitz (Java Servlet)
  * IEEE 802.15.4 LoWPAN convergence layer by Stefan Schmidt

 Routing Modules
  * Routing with static connections
  * Forward bundles on discovery
  * Epidemic routing with bloomfilter
  * Flooding routing scheme
  * PRoPHET Routing

 Storage Modules
  * Memory-based storage
  * Persistent storage in file-system
  * SQLite Storage

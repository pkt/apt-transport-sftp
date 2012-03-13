apt-transport-sftp
==================

This is a transport method for apt that supports URLs of the form

> 
> sftp://host:port/path distro components
> 

It is built on top of apt-pkg and libssh2 which should make for very
little boring maintainance work due to reasonably stable APIs.
In addition, because it is written in C++, uses apt-pkg and tries
to follow APT coding style, this version will be easier to merge
to upstream APT sources if the upstream devs accept it (compared
to the earlier version of apt-transport-sftp from Bernard Link).

Features supported for now are public-key based authentication
and download resuming.

Debian packages will be provided from my "testing" ppa.
(ppa:pktoss/testing).


TESTING
-------

Run ./runtests in the test/ folder. The tests use a "special"
"mock" sftp server written in python / paramiko that breaks
transfers bigger than 4.8M.


CREDITS
-------

  * People behind APT

  * Bernard R. Link for the first version of apt-transport-sftp
    (although this version doesn't share any code with his version)

Enjoy!

--Pantelis

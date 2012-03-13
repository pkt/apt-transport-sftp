/* vim:set expandtab tabstop=3 fdm=marker: */
// Description                                                          /*{{{*
/* ######################################################################

   SFTP Aquire Method - An SFTP aquire method for APT using libssh2

   ##################################################################### */
                                                                        /*}}}*/
#ifndef __SFTP_H__
#define __SFTP_H__
// Include Files                                                        /*{{{*
#include <apt-pkg/acquire-method.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/strutl.h>

#include <string>
#include <sys/time.h>

#include <string>
#include <libssh2.h>
#include <libssh2_sftp.h>

#include "connect.h"
#include "rfc2553emu.h"
                                                                        /*}}}*/
// SftpConn: Encapsulates SFTP-specific functionality                   /*{{{*
// ---------------------------------------------------------------------
class SftpConn
{
    char Buffer[16*1024];

    unsigned long Len;
    bool Debug;

    URI ServerName;
    int ServerFd;

    LIBSSH2_SESSION *session;
    LIBSSH2_SFTP *sftp_session;

    int TimeOut;
    std::string Username;
    std::string PubkeyFile;
    std::string PrivkeyFile;
    std::string KnownHosts;

    // Private helper functions
    void Configure(pkgAcqMethod*, const string&, int);
    bool SetupSSH();
    bool VerifyServerFingerprint();
    bool VerifyAgainstFile(const char *path, const char *fingerprint, size_t len, int type);
    bool Login();
    bool SetupSFTP();

    public:

    bool Comp(URI Other) {return Other.Host == ServerName.Host && Other.Port == ServerName.Port && Other.User == ServerName.User && Other.Password == ServerName.Password; };

    // Connection Control
    bool Open(pkgAcqMethod *Owner);
    void Close();

    // Query
    bool Attrs(const char *Path,unsigned long long &Size, time_t &Time);
    bool Get(const char *Path,FileFd &To,unsigned long long Resume,
	     bool &Missing);

    SftpConn(URI Srv);
    ~SftpConn();
};
                                                                        /*}}}*/
// SftpMethod: The Method class for interfacing with APT                /*{{{*
// ---------------------------------------------------------------------
class SftpMethod : public pkgAcqMethod
{
    static std::string FailFile;
    static int FailFd;
    static time_t FailTime;

    static void SigTerm(int);

    SftpConn *Server;

    virtual bool Fetch(FetchItem *Itm);

    public:

    SftpMethod();
};
                                                                        /*}}}*/
#endif

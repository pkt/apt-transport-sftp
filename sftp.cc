/* vim:set expandtab tabstop=3 fdm=marker: */
// Description                                                          /*{{{*
/* ######################################################################

   SFTP Aquire Method - An SFTP aquire method for APT using libssh2

   This is a simple blocking SFTP method implementation based on libssh2
   It supports basic features like If-Modified-Since and Resuming.

   ##################################################################### */
                                                                        /*}}}*/
// Include Files                                                        /*{{{*
#include "sftp.h"

#include <apt-pkg/acquire-method.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/error.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <utime.h>

#include <string>
#include <iostream>
                                                                        /*}}}*/
// Globals                                                              /*{{{*
std::string SftpMethod::FailFile;
int SftpMethod::FailFd      = -1;
time_t SftpMethod::FailTime = 0;
                                                                        /*}}}*/
// Constructor (SftpMethod)                                             /*{{{*/
// ---------------------------------------------------------------------
SftpMethod::SftpMethod() : pkgAcqMethod("1.0", SendConfig)
{
    signal(SIGTERM, SigTerm);
    signal(SIGINT, SigTerm);

    Server = 0;
    FailFd = -1;
}
                                                                        /*}}}*/
// SftpMethod::SigTerm - Handle a fatal signal                          /*{{{*/
// ---------------------------------------------------------------------
/* This closes and timestamps the open file. This is neccessary to get
   resume behavoir on user abort */
void SftpMethod::SigTerm(int)
{
   if (FailFd == -1)
       _exit(100);

   close(FailFd);
   struct utimbuf UBuf;
   UBuf.actime = FailTime;
   UBuf.modtime = FailTime;
   utime(FailFile.c_str(),&UBuf);

   _exit(100);
}
                                                                        /*}}}*/
// SftpMethod::Fetch - Fetch a file                                     /*{{{*/
// ---------------------------------------------------------------------
bool SftpMethod::Fetch(FetchItem *Itm)
{
   URI Get = Itm->Uri;
   const char *File = Get.Path.c_str();
   FetchResult Res;
   Res.Filename = Itm->DestFile;
   Res.IMSHit = false;

   // Connect to the server
   if (Server == 0 || Server->Comp(Get) == false)
   {
       delete Server;
       Server = new SftpConn(Get);
   }

   // Could not connect is a transient error..
   if (Server->Open(this) == false)
   {
        Server->Close();
        Fail(true);
        return true;
   }

   // Get the file information
   Status("Querying file attributes ...");
   unsigned long long Size;
   if (Server->Attrs(File,Size,FailTime) == false)
   {
       Fail(true);
       return true;
   }
   Res.Size = Size;

   // See if it is an IMS hit
   if (Itm->LastModified == FailTime)
   {
       Res.Size = 0;
       Res.IMSHit = true;
       URIDone(Res);
       return true;
   }

   // See if the file exists
   struct stat Buf;
   if (stat(Itm->DestFile.c_str(),&Buf) == 0)
   {
      if (Size == (unsigned long long)Buf.st_size && FailTime == Buf.st_mtime)
      {
          Res.Size = Buf.st_size;
          Res.LastModified = Buf.st_mtime;
          Res.ResumePoint = Buf.st_size;
          URIDone(Res);
          return true;
      }
 
      // Resume?
      if (FailTime == Buf.st_mtime && Size > (unsigned long long)Buf.st_size)
         Res.ResumePoint = Buf.st_size;
   }


   // Open the file
   {
       FileFd Fd(Itm->DestFile,FileFd::WriteAny);
       if (_error->PendingError() == true)
          return false;

       URIStart(Res);

       FailFile = Itm->DestFile;
       FailFile.c_str();   // Make sure we dont do a malloc in the signal handler
       FailFd = Fd.Fd();

       bool Missing;

       if (Server->Get(File,Fd,Res.ResumePoint,Missing) == false ||
          Fd.Size() < Size)
       {
           Fd.Close();

           // Timestamp
           struct utimbuf UBuf;
           UBuf.actime = FailTime;
           UBuf.modtime = FailTime;
           utime(FailFile.c_str(),&UBuf);

           // If the file is missing we hard fail and delete the destfile
           // otherwise transient fail
           if (Missing == true)
           {
               unlink(FailFile.c_str());
               return false;
           }

           Fail(true);
           return true;
       }

       Res.Size = Fd.Size();
    }

    Res.LastModified = FailTime;

    // Timestamp
    struct utimbuf UBuf;
    UBuf.actime = FailTime;
    UBuf.modtime = FailTime;
    utime(Queue->DestFile.c_str(),&UBuf);
    FailFd = -1;

    URIDone(Res);

    return true;
}
                                                                        /*}}}*/
// Constructor                                                          /*{{{*/
// ---------------------------------------------------------------------
SftpConn::SftpConn(URI Srv) : Len(0), ServerName(Srv), ServerFd(-1),
                              session(0), sftp_session(0)
{
   Debug = _config->FindB("Debug::Acquire::sftp",false);
}
                                                                        /*}}}*/
// Destructor                                                           /*{{{*/
// ---------------------------------------------------------------------
SftpConn::~SftpConn()
{
    Close();
}
                                                                        /*}}}*/
// SftpConn::Open - Open a new connection                               /*{{{*/
// ---------------------------------------------------------------------
/* Connect to the server, verify against known_hosts and authenticate */
bool SftpConn::Open(pkgAcqMethod *Owner)
{
   int Port = 22;
   std::string Host;

   // Use the already open connection if possible.
   if (ServerFd != -1)
      return true;

   Close();

   if (ServerName.Port != 0)
      Port = ServerName.Port;
   Host = ServerName.Host;

   Configure(Owner, Host, Port);

   /* Connect to the remote server. Since SFTP is connection-oriented
      we want to make sure we get a new server every time we reconnect */
   RotateDNS();
   if (!Connect(Host,Port,"sftp",22,ServerFd,TimeOut,Owner))
      goto error;

   if (!SetupSSH())
      goto error;

   if (!VerifyServerFingerprint())
      goto error;

   if (!Login())
      goto error;

   if (!SetupSFTP())
      goto error;

   return true;

error:
   Close();
   return false;
}
                                                                        /*}}}*/
// SftpConn::Close - Close the SFTP connection                          /*{{{*/
// ---------------------------------------------------------------------
void SftpConn::Close()
{
    if (sftp_session) {
        libssh2_sftp_shutdown(sftp_session);
        sftp_session = 0;
    }

    if (session) {
        libssh2_session_disconnect(session, "Closing session ...");
        libssh2_session_free(session);
        session = 0;
    }

    close(ServerFd);

    ServerFd = -1;
}
                                                                        /*}}}*/
// SftpConn::Attrs - Return the size and mtime of a file                /*{{{*/
// ---------------------------------------------------------------------
/* Grab the file size and mtime from the server                          */
bool SftpConn::Attrs(const char *Path,unsigned long long &Size,time_t &Time)
{
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc = libssh2_sftp_stat_ex(sftp_session, Path, strlen(Path),
             LIBSSH2_SFTP_STAT, &attrs);

    Size = 0;
    Time = time(&Time);

    if (rc) return false;
    
    if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)
        Size = attrs.filesize;

    if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
        Time = attrs.mtime;

    return true;
}
                                                                        /*}}}*/
// SftpConn::Get - Download a file from the Server                      /*{{{*/
// ---------------------------------------------------------------------
bool SftpConn::Get(const char *Path,FileFd &To,unsigned long long Resume,
                   bool &Missing)
{
    Missing = false;
    bool res = false;

    LIBSSH2_SFTP_HANDLE *sftp_handle;
    sftp_handle = libssh2_sftp_open(sftp_session, Path, LIBSSH2_FXF_READ, 0);
    if (!sftp_handle) {
        Missing=true;
        goto error; 
    }

    if (To.Truncate(Resume) == false)
        goto error;

    if (To.Seek(Resume) == false)
        goto error;

    libssh2_sftp_seek64(sftp_handle, (libssh2_uint64_t) Resume);

    /* loop until we fail */
    while(1)
    {
        int nbytes = libssh2_sftp_read(sftp_handle, Buffer, sizeof(Buffer));
        if (nbytes <= 0)
            break;

        if (!To.Write(Buffer,nbytes))
            goto error;
    }

    res = true;

error:
    libssh2_sftp_close(sftp_handle);
    return res;
}
                                                                        /*}}}*/
// SftpConn::Get - Download a file from the Server                      /*{{{*/
// ---------------------------------------------------------------------
bool SftpConn::SetupSSH()
{
    /* create an ssh session instance */
    session = libssh2_session_init();
    if (!session) goto error1;

    /* set it to blocking mode */
    libssh2_session_set_blocking(session, 1);

    /* ... start it up. This will trade welcome banners, exchange keys,
     * and setup crypto, compression, and MAC layers
     */
    if (libssh2_session_startup(session, ServerFd) != 0) goto error2;

    return true;

error2:
    if (Debug)
        std::clog << "Setting up ssh session failed! (low-level bug?)\n";

    libssh2_session_free(session);
    session = 0;
error1:
    return false;
}
                                                                        /*}}}*/
// SftpConn::VerifyServerFingerprint - verify against known_hosts       /*{{{*/
// ---------------------------------------------------------------------
bool SftpConn::VerifyServerFingerprint()
{
    LIBSSH2_KNOWNHOSTS *nh = libssh2_knownhost_init(session);
    if (!nh) goto error;

    int type;
    size_t len;
    const char *fingerprint;
    fingerprint = libssh2_session_hostkey(session, &len, &type);
    if (!fingerprint) goto error;

    if (!VerifyAgainstFile(KnownHosts.c_str(),fingerprint,len,type))
    	goto error;

    return true;

error:
    if (Debug)
        std::clog << "Failed verifying Server key from known_hosts.\n";
    return false;
}
                                                                        /*}}}*/
// SftpConn::VerifyAgainstFile - verify fingerprint against a file      /*{{{*/
// ---------------------------------------------------------------------
bool SftpConn::VerifyAgainstFile(const char *path, const char *fingerprint, size_t len, int type)
{
    LIBSSH2_KNOWNHOSTS *nh;
    nh = libssh2_knownhost_init(session);
    if (!nh) return false;

    libssh2_knownhost_readfile(nh,path,LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    struct libssh2_knownhost *host;
    int check = libssh2_knownhost_checkp(nh, ServerName.Host.c_str(), ServerName.Port,
                                         fingerprint, len,
                                         LIBSSH2_KNOWNHOST_TYPE_PLAIN|
                                         LIBSSH2_KNOWNHOST_KEYENC_RAW,
                                         &host);

    bool res = true;
    if (check != LIBSSH2_KNOWNHOST_CHECK_MATCH) {
        if (Debug)
            std::clog << "Host key fingerprint doesn't match the one from known_hosts file\n";
        res = false;
    }

    libssh2_knownhost_free(nh);
    return res;
}
                                                                        /*}}}*/
// SftpConn::Login - Authenticate using public key auth                 /*{{{*/
// ---------------------------------------------------------------------
bool SftpConn::Login()
{
    char *userauthlist;
    userauthlist = libssh2_userauth_list(session, Username.c_str(), Username.length());

    if (!strstr(userauthlist, "publickey")) goto error;

    if (libssh2_userauth_publickey_fromfile(session, Username.c_str(),
        PubkeyFile.c_str(), PrivkeyFile.c_str(), "") != 0) goto error;

    return true;

error:
    if (Debug)
        std::clog << "Key-based authentication failed\n";
    return false;
}
                                                                        /*}}}*/
// SftpConn::SetupSFTP - Setup SFTP session                             /*{{{*/
// ---------------------------------------------------------------------
bool SftpConn::SetupSFTP()
{
    sftp_session = libssh2_sftp_init(session);
    if (sftp_session)
        return true;

    if (Debug)
        std::clog << "Setting up the SFTP session failed\n";
    return false;
}
                                                                        /*}}}*/
// SftpConn::Configure - Initialize Config variables                    /*{{{*/
// ---------------------------------------------------------------------
void SftpConn::Configure(pkgAcqMethod *Owner, const std::string &Host, int Port)
{
    char *name = NULL;
    
    char port[10];
    snprintf(port, sizeof(port), "%d", Port);
    std::string remotehost = Host + ":" + port;
    std::string pref = "Acquire::sftp::";
    std::string hostpref = pref + remotehost + "::";

    //first the Username
    if (!ServerName.User.empty()) {
        Username = ServerName.User;
        goto done_username;
    }

    Username = _config->Find((hostpref + "Username").c_str(),
               _config->Find((pref + "Username").c_str(), ""));

    if (!Username.empty())
        goto done_username;

    name = getenv("USER");
    if (name) {
        Username = std::string(name);
        goto done_username;
    }

    name = getenv("LOGNAME");
    if (name) {
        Username = std::string(name);
        goto done_username;
    }

    Username = "mirror";

done_username:

    KnownHosts = _config->Find((hostpref + "KnownHosts").c_str(),
                 _config->Find((pref + "KnownHosts").c_str(), "/etc/ssh/known_hosts"));

    PubkeyFile = _config->Find((hostpref + "PubkeyFile").c_str(),
                 _config->Find((pref + "Pubkeyfile").c_str(), ""));

    PrivkeyFile = _config->Find((hostpref + "PrivkeyFile").c_str(),
                 _config->Find((pref + "PrivkeyFile").c_str(), ""));

    if (Debug) {
        std::clog << "Username=(" << Username.c_str() << "), KnownHosts=(" << KnownHosts.c_str() << ")\n";
        std::clog << "PubkeyFile=(" << PubkeyFile.c_str() << "), PrivkeyFile=(" << PrivkeyFile.c_str() << ")\n";
    }
}
                                                                        /*}}}*/
// main                                                                 /*{{{*/
// ---------------------------------------------------------------------
int main()
{
    setlocale(LC_ALL, "");

    int err = libssh2_init(0);
    if (err) return 126;
 
    SftpMethod Mth;

    bool res = Mth.Run();

    libssh2_exit();

    return res;
}
                                                                        /*}}}*/

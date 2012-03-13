Acquire {
  sftp {
    timeout::myhost:2222=40;
    Username::myhost:2222=repo;
    PrivkeyFile::myhost:2222=/var/srepo/sftp;
    PubkeyFile::myhost:2222=/var/srepo/sftp.pub;
  };
};

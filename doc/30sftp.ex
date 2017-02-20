Acquire {
  sftp {
    timeout 40;
    Username repo;
    PrivkeyFile /var/srepo/sftp;
    PubkeyFile /var/srepo/sftp.pub;
  };
};

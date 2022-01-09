Copyright (c) 2009-2012 Bitcoin Developers
Distributed under the MIT/X11 software license, see the accompanying
file license.txt or http://www.opensource.org/licenses/mit-license.php.
This product includes software developed by the OpenSSL Project for use in
the OpenSSL Toolkit (http://www.openssl.org/).  This product includes
cryptographic software written by Eric Young (eay@cryptsoft.com) and UPnP
software written by Thomas Bernard.


LINUX BUILD NOTES
================
### Compiling DigitalNote "SatoshiCore" daemon on Ubunutu 18.04 LTS Bionic

The guide should be compatible with other Ubuntu versions from 14.04+

Please follow the instructions below:
------------------------------------

### Become poweruser
```
sudo -i
```
### CREATE SWAP FILE FOR DAEMON BUILD (if system has less than 2GB of RAM)
```
cd ~
sudo fallocate -l 3G /swapfile
ls -lh /swapfile
sudo chmod 600 /swapfile
ls -lh /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
sudo swapon --show
sudo cp /etc/fstab /etc/fstab.bak
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

### Dependencies install
```
cd ~
sudo apt-get install -y ntp
sudo apt-get install -y git
sudo apt-get install -y build-essential
sudo apt-get install -y libssl-dev
sudo apt-get install -y libdb-dev
sudo apt-get install -y libdb++-dev
sudo apt-get install -y libboost-all-dev
sudo apt-get install -y libqrencode-dev
sudo apt-get install -y libcurl4-openssl-dev
sudo apt-get install -y curl
sudo apt-get install -y libzip-dev

sudo apt-get update -y

sudo apt-get install -y git
sudo apt-get install -y make
sudo apt-get install -y automake
sudo apt-get install -y yasm
sudo apt-get install -y binutils
sudo apt-get install -y libcurl4-openssl-dev
sudo apt-get install -y openssl
sudo apt-get install -y libgmp-dev
sudo apt-get install -y libtool
sudo apt-get install -y qt5-default
sudo apt-get install -y qttools5-dev-tools
sudo apt-get install -y miniupnpc
sudo apt-get install -y qt5-qmake
sudo apt-get install -y libevent-dev
```

### Dependencies build and link
```
cd ~;
wget http://download.oracle.com/berkeley-db/db-6.2.32.NC.tar.gz
tar zxf db-6.2.32.NC.tar.gz
cd db-6.2.32.NC/build_unix
../dist/configure --enable-cxx --disable-shared
make
sudo make install
sudo ln -s /usr/local/BerkeleyDB.6.2/lib/libdb-6.2.so /usr/lib/libdb-6.2.so
sudo ln -s /usr/local/BerkeleyDB.6.2/lib/libdb_cxx-6.2.so /usr/lib/libdb_cxx-6.2.so
export BDB_INCLUDE_PATH="/usr/local/BerkeleyDB.6.2/include"
export BDB_LIB_PATH="/usr/local/BerkeleyDB.6.2/lib"
```

### GitHub pull (Source Download)
```
cd ~
git clone https://github.com/DigitalNoteXDN/DigitalNote-2.git DigitalNote
cd DigitalNote
```

### Build DigitalNote daemon

With UPNP:
```
qmake -qt=qt5 DigitalNote.daemon.pro USE_UPNP=1
make -j 4
sudo cp -r DigitalNoted /usr/local/bin/DigitalNoted
```

**Recommended Without** UPNP:
```
qmake -qt=qt5 DigitalNote.daemon.pro
make -j 4
sudo cp -r DigitalNoted /usr/local/bin/DigitalNoted
```

### (Optional) Build DigitalNote-QT (GUI wallet) on Linux 

**All previous steps must be completed first.**

If you recompiling some other time you don't have to repeat previous steps, but need to define those variables. Skip this command if this is your first build and previous steps were performed in current terminal session.
```
export BDB_INCLUDE_PATH="/usr/local/BerkeleyDB.6.2/include"
export BDB_LIB_PATH="/usr/local/BerkeleyDB.6.2/lib"
```

With UPNP:
```
qmake -qt=qt5 DigitalNote.app.pro USE_UPNP=1 USE_DBUS=1 USE_QRCODE=1
make -j 4
```

**Recommended Without** UPNP:
```
qmake -qt=qt5 DigitalNote.app.pro USE_DBUS=1 USE_QRCODE=1
make -j 4
```


### Create config file for daemon
```
sudo ufw allow 18092/tcp
sudo ufw allow 18094/tcp
sudo ufw allow 22/tcp
sudo mkdir ~/.XDN

cat << "CONFIG" >> ~/.XDN/DigitalNote.conf
listen=1
server=1
daemon=1
testnet=0
rpcuser=XDNrpcuser
rpcpassword=SomeCrazyVeryVerySecurePasswordHere
rpcport=18094
port=18092
rpcconnect=127.0.0.1
rpcallowip=127.0.0.1
CONFIG

chmod 700 ~/.XDN/DigitalNote.conf
chmod 700 ~/.XDN
ls -la ~/.XDN
```

### Run DigitalNote daemon
```
cd ~
DigitalNoted
DigitalNoted getinfo
```

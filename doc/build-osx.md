Mac OS X DigitalNoted build instructions vgulkevic. Find me on Discord if you have any issues - https://discordapp.com/invite/4dUquty

- Tested on 10.15.7.  

You need to install XCode with all the options checked so that the compiler and
everything is available in /usr not just /Developer 
You can get the current version from http://developer.apple.com

1. Clone the github tree to get the source code

    ```git clone https://github.com/DigitalNoteXDN/DigitalNote-2 DigitalNote-2``` 

2. Install dependencies using Homebrew
   1. Install dependencies:
       ```
       brew install boost
       brew install miniupnpc
       brew install openssl
       brew install automake
       brew install autoconf
       brew install libtool
       brew install qrencode
       ```
   2. Install Berkeley-DB@6
       Download:
       
       ```
       curl -OL http://download.oracle.com/berkeley-db/db-6.2.32.tar.gz
       ```
       
       Unzip:
       ```
       tar -xf db-6.2.32.tar.gz
       ```
       Build:
       ```
       cd db-6.2.32/build_unix                                              &&
       ../dist/configure --prefix=/usr/local/Cellar/berkeley-db@6.2.32      \
                         --enable-cxx                                       &&
       make
       ```
      
       If you get compile errors in `atomic.c` you need to apply a small patch and run the 'build' command above again: 
       ```
        cd ../..
        curl -OL https://raw.githubusercontent.com/macports/macports-ports/cb92cb90bdc7fb90212e928db32172546eca0f5b/databases/db60/files/patch-src_dbinc_atomic.h
        mv patch-src_dbinc_atomic.h db-6.2.32
        cd db-6.2.32
        patch -s -p0 < patch-src_dbinc_atomic.h
        cd ..
       ```
      
       Install:
       ```
        inside db-6.2.32/build_unix folder run:
      
        sudo make install
       ```
   3. You might need to create a symlink if `openssl/sha.h` or any other header from openssl/ folder cannot be found when building DigitalNoted:
       ```
       cd /usr/local/include
       ln -s ../opt/openssl/include/openssl .
       ```
      Your compiler will search in this directory (one of many standard directories) and find the header file sha.h via the shortcut link.
   4. Now create a symlink to miniupnpc files since it is installed in a folder without a version appended and source files are expecting a version:
       ```
       cd /usr/local/include
       ln -s ../opt/miniupnpc/include/miniupnpc ./miniupnpc-2.1
       ``` 
   5. Check the versions of dependencies in src/makefile.osx and amend to match yours if required 
        
3.  Now you should be able to build DigitalNoted. Either use makefile to build daemon only or see the Step 3 of the section below (DigitalNote-qt: Qt5 GUI Release for DigitalNote) that uses .pro file:

    ```
    cd DigitalNote/src
    make -f makefile.osx
    ```

Run:
  `./DigitalNoted --help` 
for a list of command-line options.
  
Run
  `./DigitalNoted -daemon`
to start the DigitalNote daemon.
  
Run
  `./DigitalNoted help`
When the daemon is running, to get a list of RPC commands


DigitalNote-qt: Qt5 GUI Release for DigitalNote
-----------------------------------------

1. Install dependencies:
   ```
   brew install qrencode
   brew install qt5
   brew install protobuf
   brew install libevent
   brew install python2.7
   sudo easy_install appscript
   ```
2. Link qt5:
   ```
   brew link qt5 --force
   ```
3. Run in the ./DigitalNote-2
   ```
   qmake RELEASE=1 USE_UPNP=1 USE_QRCODE=1 DigitalNote.pro
   make
   python2.7 contrib/macdeploy/macdeployqtplus DigitalNote-Qt.app -add-qt-tr da,de,es,hu,ru,uk,zh_CN,zh_TW -dmg -fancy contrib/macdeploy/fancy.plist
   ```
   
4. For builds upwards of 2.0.0.4 do the following instead of step 3.
   Run in the ./DigitalNote-2
   ```
   cd DigitalNote 
   cd src
   cd leveldb 
   make libleveldb.a libmemenv.a
   cd ../../
   qmake DigitalNote.app.pro USE_UPNP=- USE_DBUS=1 USE_QRCODE=1 USE_UNITY_BUILD=1
   make -j 4
   python2.7 contrib/macdeploy/macdeployqtplus DigitalNote-qt.app -add-qt-tr da,de,es,hu,ru,uk,zh_CN,zh_TW -dmg -fancy contrib/macdeploy/fancy.plist
   ```
  
 

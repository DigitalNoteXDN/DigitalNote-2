DigitalNote Daemon/QT-Wallet
============================================
![http://www.digitalnote.org](doc/digitalnote_logo.png)

Official website: [► Digitalnote.org](http://www.digitalnote.org)
White paper: [► WHITEPAPER](https://digitalnote.org/wp-content/uploads/2020/02/DigitalNote_Whitepaper.pdf).


Support Contact
-------------------
Twitter: [► digitalnotexdn](https://twitter.com/digitalnotexdn)
Telegram: [► invite link](https://t.me/XDNDN)
Discord: [► invite link](https://discord.gg/4dUquty)


Official Download
-------------------
Official executables releases you can find [Digitalnote github](https://github.com/DigitalNoteXDN/DigitalNote-2/releases)


Build instructions:
-------------------
MacOS: [build-osx](/doc/build-osx.md)
Windows: [build-msw](/doc/build-msw.md)
Linux: [build-lnx](/doc/build-lnx.md)


Specifications and General info
-------------------
DigitalNote uses 

	* libsecp256k1,
	* libgmp,
	* Boost1.68, OR Boost1.58,  
	* Openssl1.02r,
	* erkeley DB 6.2.32,
	* QT5.12.1,
	* to compile

General Specs

	* Block Spacing: 2 Minutes
	* Stake Minimum Age: 15 Confirmations (PoS-v3) | 30 Minutes (PoS-v2)
	* Port: 18092
	* RPC Port: 18094


License
-------------------

DigitalNote [XDN] is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.


Development Process
-------------------

The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/DigitalNoteXDN/DigitalNote-1) are created
regularly to indicate new official, stable release versions of DigitalNote [XDN].

The Developer Team [Discord](https://discord.gg/4dUquty) should be used to discuss complicated or controversial changes before working
on a patch set.


Testing
-------------------

Developers work in their own trees, then submit pull requests when they think their feature or bug fix is ready.

The patch will be accepted if there is broad consensus that it is a good thing. Developers should expect to rework and resubmit patches if they don't match the project's coding conventions (see doc/coding.txt) or are controversial.

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

The master branch is regularly built and tested, but is not guaranteed to be completely stable. Tags are regularly created to indicate new stable release versions of DigitalNote.

Manual Quality Assurance (QA) Testing
-------------------

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.

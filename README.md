DigitalNote [XDN] 2014-2018 (CryptoNote Base), 2018-2020 (Current) integration/staging tree
===========================================================================================
![http://www.digitalnote.org](doc/digitalnote_logo.png)

http://www.digitalnote.org

What is the DigitalNote [XDN] Blockchain?
-----------------------------------------
To save on space and instructions please check out our [► WHITEPAPER](https://digitalnote.org/wp-content/uploads/2020/02/DigitalNote_Whitepaper.pdf).

**Support Contact**
-------------------
Developer Discord can be found at https://discord.gg/eSb7nEx.

Changes
-------
Please check the [Changelog](CHANGELOG.md) for full information.




Specifications and General info
------------------
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

BUILD MacOS
-----------
Please check the [build-osx](/docs/build-osx.md) for full instructions.

BUILD Windows
-----------
Please check the [build-msw](/docs/build-msw.md) for full instructions.

BUILD Linux
-----------
Please check the [build-lnx](/docs/build-lnx.md) for full instructions.



License
-------

DigitalNote [XDN] is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/DigitalNoteXDN/DigitalNote-1) are created
regularly to indicate new official, stable release versions of DigitalNote [XDN].

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).

The developer [mailing list](https://lists.linuxfoundation.org/mailman/listinfo/bitcoin-dev)
should be used to discuss complicated or controversial changes before working
on a patch set.



Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write [unit tests](/doc/unit-tests.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with: `make check`

There are also [regression and integration tests](/qa) of the RPC interface, written
in Python, that are run automatically on the build server.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.

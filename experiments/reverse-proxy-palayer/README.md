# cs244b-testing
## reverse-proxy-palayer

Basic implementation of proxy server that will be able to:

- emulate delay/failure of packet delivery

- build routes of requests

- controlled states explore


Codebase default configured for 2 nodes with ports: 24001, 24002

#### Build and run
to build Jar binaries and start the nodes

'''

./gradlew bootJar

java -jar ./build/libs/ReverseProxyPlayer-1.0-SNAPSHOT.jar &


'''


#!/usr/bin/env python

import sys, socket, requests, grequests, yaml, json
from optparse import OptionParser

PORT = 8111
API_STRING = '/v1/clients'

def get_my_address():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(('8.8.8.8', 80))
    result = s.getsockname()[0]
    s.close()
    return result

def get_all_hosts_by_subnet(ip):
    prefix = ip[:ip.rfind('.')+1]
    return [prefix + str(i + 1) for i in range(30)]

def make_request_host(host):
    return 'http://' + host + ':' + str(PORT) + API_STRING

def scan_subnet(ip):
    print "Scanning subnet... "
    all_hosts = get_all_hosts_by_subnet(ip)
    requests = (grequests.get(make_request_host(host), timeout=0.1) for host in all_hosts)
    responses = zip(all_hosts, grequests.map(requests))
    return [z[0] for z in responses if z[1] is not None  and z[1].status_code == 200]
    
parser = OptionParser()
parser.add_option("-f", "--file", dest="filename", help="read configuration from FILE", metavar="FILE")

(options, args) = parser.parse_args()

if options.filename is not None:
    stream = open(options.filename, "r")
else:
    print "Using stdin.  type ^D twice to end configuration input."
    stream = sys.stdin

try:
    config = yaml.load(stream)
except yaml.YAMLError as exc:
    print(exc)
print config
s = json.dumps(config['server'], default=lambda o: o.__dict__)

c = json.dumps(config['clients'], default=lambda o: o.__dict__)
print s
print c
ip = get_my_address()
res = scan_subnet(ip)
print res

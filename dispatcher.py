#!/usr/bin/env python

import sys, socket, requests, grequests, yaml, json, itertools
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
    stream = sys.stdin

try:
    config = yaml.load(stream)
except yaml.YAMLError as exc:
    print(exc)

var_vals = []
var_keys = []

for k, v in config['Variables'].iteritems():
    if 'Values' in v:
        if isinstance(v['Values'], dict):
            var_vals.append(v['Values'].values())
            var_keys.append(k)
        elif isinstance(v['Values'], list):
            var_vals.append(v['Values'])
            var_keys.append(k)

var_perms = []

for perm in itertools.product(*var_vals):
    var_perms.append(dict(zip(var_keys, perm)))

server_cmd = config['Server']['CmdlineTemplate']
client_cmd = config['Clients']['CmdlineTemplate']

for perm in var_perms:
    print server_cmd.format(**perm)
    print client_cmd.format(**perm)
    print ""


ip = get_my_address()
res = scan_subnet(ip)
print res

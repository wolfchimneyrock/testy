#!/usr/bin/env python

import sys, socket, requests, grequests, yaml, json, itertools, time
from optparse import OptionParser

PORT = 8111
API_STRING = '/v1/client'

def get_my_address():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(('8.8.8.8', 80))
    result = s.getsockname()[0]
    s.close()
    return result

def get_all_hosts_by_subnet(ip):
    prefix = ip[:ip.rfind('.')+1]
    return [prefix + str(i + 1) for i in range(240)]

def make_request_host(host, resource):
    return 'http://' + host + ':' + str(PORT) + resource

def scan_subnet(ip):
    print "Scanning subnet... "
    all_hosts = get_all_hosts_by_subnet(ip)
    requests = (grequests.get(make_request_host(host, API_STRING), timeout=0.1) for host in all_hosts)
    responses = zip(all_hosts, grequests.map(requests))
    return [z[0] for z in responses if z[1] is not None  and z[1].status_code == 200]
   
def hook_location(*hargs, **hkwargs):
    def client_location_hook(response, *args, **kwargs):
        if 'host' in hkwargs and 'location' in hkwargs and 'Location' in response.headers:
            url = make_request_host(hkwargs['host'], response.headers['Location'])
            print "starting client at " + url
            hkwargs['location'].append(url)
    return client_location_hook

def hook_data(*hargs, **hkwargs):
    def client_data_hook(response, *args, **kwargs):
        if 'output' in hkwargs:
            for data in response.content.splitlines():
                s = ""
                if 'perm' in hkwargs:
                    s = hkwargs['perm'] + ","
                s = s + data
                print s;
                hkwargs['output'].append(s)

    return client_data_hook

if __name__ == "__main__":

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
    var_perms = []
    
    if 'Variables' in config:
        for k, v in config['Variables'].iteritems():
            if 'Values' in v:
                if isinstance(v['Values'], dict):
                    var_vals.append(v['Values'].keys())
                    var_keys.append(k)
                elif isinstance(v['Values'], list):
                    var_vals.append(v['Values'])
                    var_keys.append(k)

        for perm in itertools.product(*var_vals):
            var_perms.append(dict(zip(var_keys, perm)))

    if 'CmdlineTemplate' in config['Server']:
        server_cmd = config['Server']['CmdlineTemplate']
    else:
        print "Error: Server:CmdlineTemplate required"
        sys.exit()

    if 'CmdlineTemplate' in config['Clients']:
        client_cmd = config['Clients']['CmdlineTemplate']
    else:
        print "Error: Clients:CmdlineTemplate required"
        sys.exit()

    if 'Duration' in config['Test']:
        duration   = config['Test']['Duration']
    else:
        duration = 60
    if 'Tick' in config['Test']:
        tick       = config['Test']['Tick']
    else:
        tick = 1

    ip = get_my_address()
    server_host  = config['Server']['Host']
    client_hosts = scan_subnet(ip)
    if 'SkipServers' in config['Clients'] and config['Clients']['SkipServers'] == True:
        if server_host in client_hosts:
            client_hosts.remove(server_host)

    print "Server:  " + server_host
    print "Clients: "
    print client_hosts
    
    if len(client_hosts) == 0:
        print "No clients found, aborting."
        sys.exit()

    print "Iterating through {} variable permutations".format(len(var_perms))

    for perm in var_perms:
        p = {}
        perm_str = ",".join((str(x) for x in perm.itervalues()))
        for key, val in perm.iteritems():
            if isinstance(config['Variables'][key]['Values'], dict):
                p[key] = config['Variables'][key]['Values'][val]
            else:
                p[key] = val
        print p
        server_body = {}
        server_body["Cmdline"] = server_cmd.format(**p)
        if 'ReadyMessage' in config['Server']: 
            server_body["ReadyMessage"]   = config['Server']['ReadyMessage']
        if 'StopSignal' in config['Server']:
            server_body['StopSignal'] = config['Server']['StopSignal']

        # server GET should be synchronous, since we require it to be done before starting clients
        server_response = requests.post(make_request_host(server_host, API_STRING + 's'), json=server_body, timeout=3600)
        if server_response.status_code == 201 and 'Location' in server_response.headers:
        # now a server instance is running, we start clients.  
            print "got successful server instance.  starting clients."
            # continue with clients
            client_str =  client_cmd.format(**perm)
            client_body = {}
            client_body['Cmdline'] = client_cmd.format(**perm)
            client_loc = []
            client_data = []
            req = (grequests.post(make_request_host(h, API_STRING + 's'), 
                hooks={'response':[hook_location(location=client_loc, host=h)]},
                json=client_body, 
                timeout=120) for h in client_hosts)
            res = grequests.map(req)

            # now do our thing
            # get start time
            start_time = time.time()
            remaining = duration
            while remaining > 0:
                time.sleep(tick)
                tick_req = (grequests.get(h, 
                    hooks={'response':[hook_data(output=client_data, perm=perm_str)]},
                    timeout=120) for h in client_loc)
                tick_res = grequests.map(tick_req)
                remaining = duration - time.time() + start_time
            
            # clients are done, delete them
            print "deleting clients..."
            del_req = (grequests.delete(h, timeout=120) for h in client_loc)
            del_res = grequests.map(del_req)

            # after clients are done, we have to shutdown server before
            # going on to the next permutation
            print "deleting server instance..."
            delete_server_response = requests.delete(make_request_host(server_host, server_response.headers['Location']), timeout=3600)
            if delete_server_response.status_code != 200:
                print "Error deleting server instance.  Quitting"
                sys.exit(1)
            #time.sleep(2)
        else:
            print "failed with error " + str(server_response.status_code)
            sys.exit(1)
            

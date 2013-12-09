#! /usr/bin/python

import sys, csv, numpy
import networkx as nx

BW_FILENAME="aggregate_mean_netspeeds.csv"
PLOSS_FILENAME="aggregate_mean_netquality.csv"
MAP_FILENAME="full_tor_map.xml"
GEO_FILENAME="geoip"
OUTPUT_FILENAME="topology.full.graphml.xml"

def main():
    bwup, bwdown = get_bandwidth() # KiB/s
    loss = get_packet_loss() # fraction between 0 and 1
    meanloss = numpy.mean(loss.values())
    keys = sorted(bwup.keys())
    for k in keys:
        if k not in loss: loss[k] = meanloss

    bupstr = ','.join(["{0}={1}".format(k, bwup[k]) for k in keys])
    bdownstr = ','.join(["{0}={1}".format(k, bwdown[k]) for k in keys])
    plossstr = ','.join(["{0}={1}".format(k, loss[k]) for k in keys])

    geo = get_geo()

    # now get the graph and mod it as needed for shadow
    Gin = nx.read_graphml(MAP_FILENAME)
    assert nx.is_connected(Gin)
    assert nx.number_connected_components(Gin) == 1
    print "G in appears OK"

    G = nx.Graph()
    G.graph['bandwidthup'] = bupstr
    G.graph['bandwidthdown'] = bdownstr
    G.graph['packetloss'] = plossstr
    # hack until we use a version of igraph that supports graph attributes
    G.add_node("dummynode", bandwidthup=bupstr, bandwidthdown=bdownstr, packetloss=plossstr)

    empty = set()
    fp_to_ip = {}

    for id in Gin.nodes():
        n = Gin.node[id]

        if 'nodetype' not in n:
            empty.add(id)
            continue

        elif 'pop' in n['nodetype']:
            popid = str(n['nodeid'])
            gc = n['countries'] if 'countries' in n else 'US'
            asnum = n['asn'].split()[0][2:]
            G.add_node(popid, nodetype='pop', geocodes=gc, asn=asnum)

        elif 'relay' in n['nodetype']:
            ip = n['relay_ip']
            fingerprint = n['id']
            #nickname = n['nick']
            asnum = n['asn'].split()[0][2:]
            #pop = n['pop']
            #poiip = n['ip']
            gc = get_geo_code(geo, ip)
            G.add_node(ip, nodetype='relay', geocodes=gc, asn=asnum)
            fp_to_ip[fingerprint] = ip

        elif 'dest' in n['nodetype']:
            ip = '.'.join(n['nodeid'].split('_')[1:])
            asnum = n['asn'].split()[0][2:]
            gc = n['country']
            G.add_node(ip, nodetype='server', geocodes=gc, asn=asnum)

    for (srcid, dstid) in Gin.edges():
        e = Gin.edge[srcid][dstid]
        s = srcid
        if 'dest' in s: s = '.'.join(s.split('_')[1:])
        elif s in fp_to_ip: s = fp_to_ip[s]

        d = dstid
        if 'dest' in d: d = '.'.join(d.split('_')[1:])
        elif d in fp_to_ip: d = fp_to_ip[d]

        if s in G.node and d in G.node: G.add_edge(s, d, latencies=e['latency'])
        else: print "skipped edge: {0} -- {1}".format(s, d)

    # connect the dummy node that stores our bandwidth information
    dummy_connect_id = G.nodes()[0]
    G.add_edge("dummynode", dummy_connect_id, latencies="10000.0")
    G.add_edge(dummy_connect_id, "dummynode", latencies="10000.0")

    # undirected graphs
    assert nx.is_connected(G)
    assert nx.number_connected_components(G) == 1

    # directed graphs
    #assert nx.is_strongly_connected(G)
    #assert nx.number_strongly_connected_components(G) == 1

    print "G out is connected with 1 component"

    nx.write_graphml(G, OUTPUT_FILENAME)

def get_packet_loss():
    loss = {}
    with open(PLOSS_FILENAME, 'rb') as f:
        r = csv.reader(f) # country, region, jitter, packetloss, latency
        for row in r:
            country, region, jitter, packetloss, latency = row[0], row[1], float(row[2]), float(row[3]), float(row[4])
            packetloss /= 100.0 # percent to fraction
            assert packetloss > 0.0 and packetloss < 1.0
            code = get_code(country, region)
            if code not in loss: loss[code] = packetloss
    return loss

def get_bandwidth():
    bwdown, bwup = {}, {}
    with open(BW_FILENAME, 'rb') as f:
        r = csv.reader(f) # country, region, bwdown, bwup
        for row in r:
            country, region = row[0], row[1]
            # kb/s -> KiB/s
            down = int(round((float(row[2]) * 1000.0) / 1024.0))
            up = int(round((float(row[3]) * 1000.0) / 1024.0))
            code = get_code(country, region)
            if code not in bwdown: bwdown[code] = down
            if code not in bwup: bwup[code] = up
    return bwdown, bwup

def get_code(country, region):
    if ('US' in country and 'US' not in region) or ('CA' in country and 'CA' not in region):
        return "{0}{1}".format(country, region)
    else:
        return "{0}".format(country)

def get_geo_code(geo, ip):
    ip_array = ip.split('.')
    ipnum = long(ip_array[0]) * 16777216 + long(ip_array[1]) * 65536 + long(ip_array[2]) * 256 + long(ip_array[3])
    for entry in geo:
        if ipnum >= entry[0] and ipnum <= entry[1]: 
            return "{0}".format(entry[2])
    return "US"

def get_geo():
    entries = []
    with open(GEO_FILENAME, "rb") as f:
        for line in f:
            if line[0] == "#": continue
            parts = line.strip().split(',')
            entry = (parts[0], parts[1], parts[2]) # lownum, highnum, countrycode
            entries.append(entry)
    return entries

def make_test_graph():
    G = nx.DiGraph()

    G.graph['bandwidthup'] = "USDC=1024,USVA=1024,USMD=968,FR=600,DE=750"
    G.graph['bandwidthdown'] = "USDC=1024,USVA=1024,USMD=968,FR=600,DE=750"
    G.graph['packetloss'] = "USDC=0.001,USVA=0.001,USMD=0.001,FR=0.001,DE=0.001"

    G.add_node("141.161.20.54", nodetype="relay", nodeid="141.161.20.54", asn=10, geocodes="USDC")
    G.add_node("1", nodetype="pop", nodeid="1", asn=10, geocodes="USDC,USVA,USMD")
    G.add_node("2", nodetype="pop", nodeid="2", asn=20, geocodes="FR,DE")
    G.add_node("137.150.145.240", nodetype="server", nodeid="137.150.145.240", asn=30, geocodes="DE")

    G.add_edge("141.161.20.54", "1", latencies="2.0,2.1,2.2,2.2,2.2,2.3,2.6,2.8,3.0,3.5")
    G.add_edge("1", "141.161.20.54", latencies="2.0,2.1,2.2,2.2,2.2,2.3,2.6,2.8,3.0,3.5")
    G.add_edge("1", "2", latencies="80.3,83.6,88.5,89.4,89.6,89.9,90.9,91.2,92.3,95.0")
    G.add_edge("2", "1", latencies="80.3,83.6,88.5,89.4,89.6,89.9,90.9,91.2,92.3,95.0")
    G.add_edge("2", "137.150.145.240", latencies="2.0,2.1,2.2,2.2,2.2,2.3,2.6,2.8,3.0,3.5")
    G.add_edge("137.150.145.240", "2", latencies="2.0,2.1,2.2,2.2,2.2,2.3,2.6,2.8,3.0,3.5")

    nx.write_graphml(G, "test.graphml.xml")

if __name__ == '__main__': sys.exit(main())
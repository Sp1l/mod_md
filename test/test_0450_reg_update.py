# test mod_md acme terms-of-service handling

import json
import os.path
import re
import shutil
import subprocess
import sys
import time
import pytest

from ConfigParser import SafeConfigParser
from datetime import datetime
from httplib import HTTPConnection
from urlparse import urlparse
from shutil import copyfile
from testbase import BaseTest

config = SafeConfigParser()
config.read('test.ini')
PREFIX = config.get('global', 'prefix')

A2MD = config.get('global', 'a2md_bin')
ACME_URL = config.get('acme', 'url')
WEBROOT = config.get('global', 'server_dir')
STORE_DIR = os.path.join(WEBROOT, 'md') 

def setup_module(module):
    print("setup_module: %s" % module.__name__)
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)


class TestReg (BaseTest):

    NAME1 = "greenbytes2.de"
    NAME2 = "test-100.com"

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)
        # wipe store directory
        print("clear store dir: %s" % STORE_DIR)
        assert len(STORE_DIR) > 1
        shutil.rmtree(STORE_DIR, ignore_errors=True)
        os.makedirs(STORE_DIR)
        # add managed domains
        dnslist = [ 
            [ self.NAME1, "www.greenbytes2.de", "mail.greenbytes2.de"],
            [ self.NAME2, "test-101.com", "test-102.com" ]
        ]
        for dns in dnslist:
            args = [A2MD, "-a", ACME_URL, "-d", STORE_DIR, "-j", "add" ]
            args.extend(dns)
            self.exec_sub(args)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    # --------- update ---------

    # update domains
    def test_100(self):
        args = [A2MD, "-d", STORE_DIR, "-j" ]
        dns = [ "foo.de", "bar.de" ]
        args.extend([ "update", self.NAME1, "domains" ])
        args.extend(dns)
        outdata = self.exec_sub(args)
        jout1 = json.loads(outdata)
        md = jout1['output'][0]
        assert md['name'] == self.NAME1
        assert md['domains'] == dns
        assert md['ca']['url'] == ACME_URL
        assert md['ca']['proto'] == 'ACME'
        assert md['state'] == 1
        # list store content
        args = [A2MD, "-d", STORE_DIR, "-j", "list" ]
        outdata = self.exec_sub(args)
        jout2 = json.loads(outdata)
        assert md == jout2['output'][0]

    # remove all domains
    def test_101(self):
        args = [A2MD, "-d", STORE_DIR, "-j" ]
        args.extend([ "update", self.NAME1, "domains" ])
        self.exec_sub_err(args, 1)

    # update domains with invalid DNS
    @pytest.mark.parametrize("invalidDNS", [
        ("tld"), ("white sp.ace"), ("*.wildcard.com"), ("k\xc3ller.idn.com")
    ])
    def test_102(self, invalidDNS):
        args = [A2MD, "-d", STORE_DIR, "-j" ]
        args.extend([ "update", self.NAME1, "domains", invalidDNS ])
        self.exec_sub_err(args, 1)

    # update domains with overlapping DNS list
    def test_103(self):
        args = [A2MD, "-d", STORE_DIR, "-j" ]
        dns = [ self.NAME1, self.NAME2 ]
        args.extend([ "update", self.NAME1, "domains" ])
        args.extend(dns)
        self.exec_sub_err(args, 1)

    # update ca URL
    def test_104(self):
        args = [A2MD, "-d", STORE_DIR, "-j" ]
        url = "http://localhost.com:9999"
        args.extend([ "update", self.NAME1, "ca", url])
        outdata = self.exec_sub(args)
        jout1 = json.loads(outdata)
        md = jout1['output'][0]
        assert md['name'] == self.NAME1
        assert md['ca']['url'] == url
        assert md['ca']['proto'] == 'ACME'
        assert md['state'] == 1

    # update ca with invalid URL
    @pytest.mark.parametrize("invalidURL", [
        ("no.schema/path"), ("http://white space/path"), ("http://bad.port:-1/path")
    ])
    def test_105(self, invalidURL):
        args = [A2MD, "-d", STORE_DIR, "-j" ]
        args.extend([ "update", self.NAME1, "ca", invalidURL])
        self.exec_sub_err(args, 1)

    # update with subdomains
    def test_106(self):
        args = [A2MD, "-d", STORE_DIR, "-j" ]
        dns = [ "test-foo.com", "sub.test-foo.com" ]
        args.extend([ "update", self.NAME1, "domains" ])
        args.extend(dns)
        outdata = self.exec_sub(args)
        jout1 = json.loads(outdata)
        md = jout1['output'][0]
        assert md['name'] == self.NAME1
        assert md['domains'] == dns

    # update domains with duplicates
    def test_107(self):
        args = [A2MD, "-d", STORE_DIR, "-j" ]
        dns = [ self.NAME1, self.NAME1, self.NAME1 ]
        args.extend([ "update", self.NAME1, "domains" ])
        args.extend(dns)
        outdata = self.exec_sub(args)
        jout1 = json.loads(outdata)
        md = jout1['output'][0]
        assert md['name'] == self.NAME1
        assert md['domains'] == [ self.NAME1 ]

    # remove domains with punycode
    def test_108(self):
        args = [A2MD, "-d", STORE_DIR, "-j" ]
        dns = [ self.NAME1, "xn--kller-jua.punycode.de" ]
        args.extend([ "update", self.NAME1, "domains" ])
        args.extend(dns)
        outdata = self.exec_sub(args)
        jout1 = json.loads(outdata)
        md = jout1['output'][0]
        assert md['name'] == self.NAME1
        assert md['domains'] == dns

    # update non-exeisting managed domain
    def test_109(self):
        args = [A2MD, "-d", STORE_DIR, "-j" ]
        args.extend([ "update", "test-foo.com", "domains" ])
        self.exec_sub_err(args, 1)
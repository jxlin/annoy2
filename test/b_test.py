# Copyright (c) 2013 Spotify AB
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

# TeamCity fails to run this test because it can't import the C++ module.
# I think it's because the C++ part gets built in another directory.

import unittest
import random
import numpy
from annoy import AnnoyIndex
import os

try:
    xrange
except NameError:
    # Python 3 compat
    xrange = range


class TestCase(unittest.TestCase):
    def assertAlmostEquals(self, x, y):
        # Annoy uses float precision, so we override the default precision
        super(TestCase, self).assertAlmostEquals(x, y, 4)


class AngularIndexTest(TestCase):

    def t1est_dist(self):
        os.system("rm -rf test_db")
        os.system("mkdir test_db")
        
        f = 2   
        i = AnnoyIndex(f,  2, "test_db", 64,  1000, 3048576000)
        
        print "creating object"
        i.add_item(0, [0, 1])
        i.add_item(1, [1, 1])

        print "creating object"
        self.assertAlmostEqual(i.get_distance(0, 1), 2 * (1.0 - 2 ** -0.5))
        print "done"

    def t1est_dist_2(self):
        os.system("rm -rf test_db")
        os.system("mkdir test_db")
        f = 2
        i = AnnoyIndex(f, 2, "test_db", 64,  1000, 3048576000)
        i.add_item(0, [1000, 0])
        i.add_item(1, [10, 0])

        self.assertAlmostEqual(i.get_distance(0, 1), 0)

    def t1est_dist_3(self):
        os.system("rm -rf test_db")
        os.system("mkdir test_db")
        f = 2
        i = AnnoyIndex(f, 2, "test_db", 64,  1000, 3048576000)
        i.add_item(0, [97, 0])
        i.add_item(1, [42, 42])

        dist = (1 - 2 ** -0.5) ** 2 + (2 ** -0.5) ** 2

        self.assertAlmostEqual(i.get_distance(0, 1), dist)

    def t1est_dist_degen(self):
        os.system("rm -rf test_db")
        os.system("mkdir test_db")
        f = 2
        i = AnnoyIndex(f, 2, "test_db", 64,  1000, 3048576000)
        
        i.add_item(0, [1, 0])
        i.add_item(1, [0, 0])

        self.assertAlmostEqual(i.get_distance(0, 1), 2.0)
    def test_get_nns_by_vector(self):
        print "test_get_nns_by_vector "
        os.system("rm -rf test_db")
        os.system("mkdir test_db")
        f = 3
        i = AnnoyIndex(f, 3, "test_db", 10, 1000, 3048576000)
        i.add_item(0, [0, 0, 1])
        i.add_item(1, [0, 1, 0])
        i.add_item(2, [1, 0, 0])
      

        self.assertEqual(i.get_nns_by_vector([3, 2, 1], 3), [2, 1, 0])
        self.assertEqual(i.get_nns_by_vector([1, 2, 3], 3), [0, 1, 2])
        self.assertEqual(i.get_nns_by_vector([2, 0, 1], 3), [2, 0, 1])

    def t1est_get_nns_by_item(self):
        print "test_get_nns_by_item "
        os.system("rm -rf test_db")
        os.system("mkdir test_db")
        f = 3
        i = AnnoyIndex(f, 3, "test_db", 10, 1000, 3048576000)
        i.add_item(0, [2, 1, 0])
        i.add_item(1, [1, 2, 0])
        i.add_item(2, [0, 0, 1])
       
        self.assertEqual(i.get_nns_by_item(0, 3), [0, 1, 2])
        self.assertEqual(i.get_nns_by_item(1, 3), [1, 0, 2])
        self.assertTrue(i.get_nns_by_item(2, 3) in [[2, 0, 1], [2, 1, 0]]) # could be either

    def t1est_large_index(self):
        os.system("rm -rf test_db")
        os.system("mkdir test_db")
        # Generate pairs of random points where the pair is super close
        f = 10
        i = AnnoyIndex(f, 10, "test_db", 10,  1000, 3048576000)
        for j in xrange(0, 10000, 2):
            p = [random.gauss(0, 1) for z in xrange(f)]
            f1 = random.random() + 1
            f2 = random.random() + 1
            x = [f1 * pi + random.gauss(0, 1e-2) for pi in p]
            y = [f2 * pi + random.gauss(0, 1e-2) for pi in p]
            i.add_item(j, x)
            i.add_item(j+1, y)

        for j in xrange(0, 10000, 2):
            self.assertEqual(i.get_nns_by_item(j, 2), [j, j+1])
            self.assertEqual(i.get_nns_by_item(j+1, 2), [j+1, j])

if __name__ == '__main__':
    unittest.main()

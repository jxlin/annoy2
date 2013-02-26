#include <stdio.h>
#include <string>
#include <boost/python.hpp>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <boost/random.hpp>
#include <boost/random/normal_distribution.hpp>

using namespace std;
using namespace boost;

template<typename T>
struct __attribute__((__packed__)) node {
  /*
   * We store a binary tree where each node has two things
   * - A vector associated with it
   * - Two children
   * All nodes occupy the same amount of memory
   * All nodes with n_descendants == 1 are leaf nodes.
   * A memory optimization is that for nodes with 2 <= n_descendants <= K,
   * we skip the vector. Instead we store a list of all descendants. K is
   * determined by the number of items that fits in the same space.
   * For nodes with n_descendants == 1 or > K, there is always a
   * corresponding vector. 
   * Note that we can't really do sizeof(node<T>) because we cheat and allocate
   * more memory to be able to fit the vector outside
   */
  int n_descendants;
  int children[2]; // Will possibly store more than 2
  T v[0]; // Hack. We just allocate as much memory as we need and let this array overflow
};

template<typename T>
class AnnoyIndex {
  /*
   * We use random projection to build a forest of binary trees of all items.
   * Basically just split the hyperspace into two sides by a hyperplane,
   * then recursively split each of those subtrees etc.
   * We create a tree like this q times. The default q is determined automatically
   * in such a way that we at most use 2x as much memory as the vectors take.
   */
  int _f;
  size_t _s;
  int _n_items;
  mt19937 _rng;
  normal_distribution<T> _nd;
  variate_generator<mt19937&, 
		    normal_distribution<T> > _var_nor;
  void* _nodes; // Could either be mmapped, or point to a memory buffer that we reallocate
  int _n_nodes;
  int _nodes_size;
  vector<int> _roots;
  int _K;
  bool _loaded;
public:
  AnnoyIndex(int f) : _rng(), _nd(), _var_nor(_rng, _nd) {
    _f = f;
    _s = sizeof(node<T>) + sizeof(T) * f; // Size of each node
    _n_items = 0;
    _n_nodes = 0;
    _nodes_size = 0;
    _nodes = NULL;
    _loaded = false;

    _K = (sizeof(T) * f + sizeof(int) * 2) / sizeof(int);
  }
  ~AnnoyIndex() {
    if (!_loaded && _nodes) {
      free(_nodes);
    }
  }

  void add_item(int item, const python::list& v) {
    _allocate_size(item+1);
    node<T>* n = _get(item);

    n->children[0] = 0;
    n->children[1] = 0;
    n->n_descendants = 1;

    for (int z = 0; z < _f; z++)
      n->v[z] = python::extract<T>(v[z]);

    if (item >= _n_items)
      _n_items = item + 1;
  }

  void build(int q) {
    _n_nodes = _n_items;
    while (1) {
      if (q == -1 && _n_nodes >= _n_items * 2)
	break;
      if (q != -1 && _roots.size() >= (size_t)q)
	break;
      printf("pass %zd...\n", _roots.size());

      vector<int> indices;
      for (int i = 0; i < _n_items; i++)
	indices.push_back(i);

      _roots.push_back(_make_tree(indices));
    }
    // Also, copy the roots into the last segment of the array
    // This way we can load them faster without reading the whole file
    _allocate_size(_n_nodes + _roots.size());
    for (size_t i = 0; i < _roots.size(); i++)
      memcpy(_get(_n_nodes + i), _get(_roots[i]), _s);
    _n_nodes += _roots.size();
      
    printf("has %d nodes\n", _n_nodes);
  }

  void save(const string& filename) {
    FILE *f = fopen(filename.c_str(), "w");
    
    fwrite(_nodes, _s, _n_nodes, f);

    fclose(f);

    free(_nodes);
    _n_items = 0;
    _n_nodes = 0;
    _nodes_size = 0;
    _nodes = NULL;
    _roots.clear();
    load(filename);
  }

  void load(const string& filename) {
    struct stat buf;
    stat(filename.c_str(), &buf);
    off_t size = buf.st_size;
    int fd = open(filename.c_str(), O_RDONLY, (mode_t)0400);
    _nodes = (node<T>*)mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);

    _n_nodes = size / _s;

    // Find the roots by scanning the end of the file and taking the nodes with most descendants
    int m = -1;
    for (int i = _n_nodes - 1; i >= 0; i--) {
      int k = _get(i)->n_descendants;
      if (m == -1 || k == m) {
	_roots.push_back(i);
	m = k;
      } else {
	break;
      }
    }
    _loaded = true;
    _n_items = m;
    printf("found %lu roots with degree %d\n", _roots.size(), m);
  }

  inline T _cos(const T* x, const T* y) {
    T pp = 0, qq = 0, pq = 0;
    for (int z = 0; z < _f; z++) {
      pp += x[z] * x[z];
      qq += y[z] * y[z];
      pq += x[z] * y[z];
    }
    T ppqq = pp * qq;
    if (ppqq > 0) return pq / sqrt(ppqq);
    else return 0.0;
  }

  inline T cos(int i, int j) {
    const T* x = _get(i)->v;
    const T* y = _get(j)->v;
    return _cos(x, y);
  }

  python::list get_nns_by_item(int item, int n) {
    const node<T>* m = _get(item);
    return _get_all_nns(m->v, n);
  }
  python::list get_nns_by_vector(python::list v, int n) {
    vector<T> w(_f);
    for (int z = 0; z < _f; z++)
      w[z] = python::extract<T>(v[z]);
    return _get_all_nns(&w[0], n);
  }

  int get_n_items() {
    return _n_items;
  }
private:
  void _allocate_size(int n) {
    if (n > _nodes_size) {
      int new_nodes_size = (_nodes_size + 1) * 2;
      if (n > new_nodes_size)
	new_nodes_size = n;
      _nodes = realloc(_nodes, _s * new_nodes_size);
      memset((char *)_nodes + (_nodes_size * _s)/sizeof(char), 0, (new_nodes_size - _nodes_size) * _s);
      _nodes_size = new_nodes_size;
    }
  }

  inline node<T>* _get(int i) {
    return (node<T>*)((char *)_nodes + (_s * i)/sizeof(char));
  }

  int _make_tree(const vector<int >& indices) {
    if (indices.size() == 1)
      return indices[0];

    _allocate_size(_n_nodes + 1);
    int item = _n_nodes++;
    node<T>* m = _get(item);
    m->n_descendants = indices.size();

    if (indices.size() <= (size_t)_K) {
      for (size_t i = 0; i < indices.size(); i++)
	m->children[i] = indices[i];
      return item;
    }

    vector<int> children_indices[2];
    for (int attempt = 0; attempt < 20; attempt ++) {
      /*
       * Create a random hyperplane.
       * If all points end up on the same time, we try again.
       * We could in principle *construct* a plane so that we split
       * all items evenly, but I think that could violate the guarantees
       * given by just picking a hyperplane at random
       */
      for (int z = 0; z < _f; z++)
	m->v[z] = _var_nor();
      
      children_indices[0].clear();
      children_indices[1].clear();

      for (size_t i = 0; i < indices.size(); i++) {
	int j = indices[i];
	node<T>* n = _get(j);
	if (!n) {
	  printf("node %d undef...\n", j);
	  continue;
	}
	T dot = 0;
	for (int z = 0; z < _f; z++) {
	  dot += m->v[z] * n->v[z];
	}
	while (dot == 0)
	  dot = _var_nor();
	children_indices[dot > 0].push_back(j);	
      }

      if (children_indices[0].size() > 0 && children_indices[1].size() > 0) {
	break;
      }
    }

    while (children_indices[0].size() == 0 || children_indices[1].size() == 0) {
      // If we didn't find a hyperplane, just randomize sides as a last option
      if (indices.size() > 100000)
	printf("Failed splitting %lu items\n", indices.size());

      children_indices[0].clear();
      children_indices[1].clear();

      // Set the vector to 0.0
      for (int z = 0; z < _f; z++)
	m->v[z] = 0.0;

      for (size_t i = 0; i < indices.size(); i++) {
	int j = indices[i];
	// Just randomize...
	children_indices[_var_nor() > 0].push_back(j);
      }
    }

    int children_0 = _make_tree(children_indices[0]);
    int children_1 = _make_tree(children_indices[1]);

    // We need to fetch m again because it might have been reallocated
    m = _get(item);
    m->children[0] = children_0;
    m->children[1] = children_1;

    return item;
  }

  void _get_nns(const T* v, int i, vector<int>* result, int limit) {
    const node<T>* n = _get(i);

    if (n->n_descendants == 0) {
      // unknown item, nothing to do...
    } else if (n->n_descendants == 1) {
      result->push_back(i);
    } else if (n->n_descendants <= _K) {
      for (int j = 0; j < n->n_descendants; j++) {
	result->push_back(n->children[j]);
      }
    } else {
      T dot = 0;

      for (int z = 0; z < _f; z++)
	dot += v[z] * n->v[z];

      while (dot == 0) // Randomize side if the dot product is zero
	dot = _var_nor();

      _get_nns(v, n->children[dot > 0], result, limit);
      if (result->size() < (size_t)limit)
	_get_nns(v, n->children[dot < 0], result, limit);
    }
  }

  python::list _get_all_nns(const T* v, int n) {
    vector<pair<T, int> > nns_cos;

    for (size_t i = 0; i < _roots.size(); i++) {
      vector<int> nns;
      _get_nns(v, _roots[i], &nns, n);
      for (size_t j = 0; j < nns.size(); j++) {
	nns_cos.push_back(make_pair(_cos(v, _get(nns[j])->v), nns[j]));
      }
    }
    sort(nns_cos.begin(), nns_cos.end());
    int last = -1, length=0;
    python::list l;
    for (int i = (int)nns_cos.size() - 1; i >= 0 && length < n; i--) {
      if (nns_cos[i].second != last) {
	l.append(nns_cos[i].second);
	last = nns_cos[i].second;
	length++;
      }
    }
    return l;
  }
};

BOOST_PYTHON_MODULE(annoylib)
{
  python::class_<AnnoyIndex<float> >("AnnoyIndex", python::init<int>())
    .def("add_item",          &AnnoyIndex<float>::add_item)
    .def("build",             &AnnoyIndex<float>::build)
    .def("save",              &AnnoyIndex<float>::save)
    .def("load",              &AnnoyIndex<float>::load)
    .def("cos",               &AnnoyIndex<float>::cos)
    .def("get_nns_by_item",   &AnnoyIndex<float>::get_nns_by_item)
    .def("get_nns_by_vector", &AnnoyIndex<float>::get_nns_by_vector)
    .def("get_n_items",       &AnnoyIndex<float>::get_n_items);
}

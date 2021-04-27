
#include <iostream>
#include <functional>
#include <algorithm>
#include <numeric>
#include <boost/bind/bind.hpp>
#include <vector>
#include <boost/typeof/typeof.hpp>

using namespace boost::placeholders;
using namespace std;

typedef function<int(int)> func;

double f(int x, int y) {
	return x / y;
}

double g(int x, int y, int z) {
	return x / y / z;
}

int test(int num) {
	cout << "in test" << endl;
	return 2;
}

int test2(int num, char sign) {
	return num;
}

void (*fun)(int a);

struct demo {
	int f(int a, int b) {
		return a + b;
	}
};

int main2() {
	const char* foo[] = { "10","20","30","40","50" };
	int bar[5];
	int sum;
	transform(foo, foo + 5, bar, ptr_fun(atoi));
	sum = accumulate(bar, bar + 5, 0);
	double ibind1 = bind1st(ptr_fun(f), 5)(1);
	double bind_1 = bind(f, 5, _1)(1);
	double ibind2 = bind2nd(ptr_fun(f), 5)(1);
	double bind_2 = bind(g, _1, _1, _1)(5, 1);
	cout << "sum=" << sum << endl;
	cout << "ibind1 " << ibind1 << endl;
	cout << "bind_2 " << bind_2 << endl;

	//====================================================
	class F {
	public:
		int operator()(int a, int b) {
			return a - b;
		}

		bool operator()(double a, double b) {
			return a == b;
		}
	};

	//F Ffuction;
	//function<int(int, int)> fclass(bind<function<int(int, int)>>(Ffuction, _1, _2)(1,3));
	
	

	//cout << "res " << fclass(1,2) << endl;

	//========================================================
	//function<int()> fclass = bind(greater<int>(), _1, 10);

	cout << bind(test, _1)(6) << endl;
	bind(test, _1)(4);//加载并运行
	bind(test, 6);//只加载没运行
	//fun = bind(test, 6);
	func fc = bind(test, 6);
	func f(bind(test, 6));

	typedef function<int(int, char)> func2;

	func2 f2;

	f2 = &test2;

	demo a, & ra = a;
	demo* p = &a;
	cout << bind(&demo::f, a, _1, 20)(10) << endl;
	cout << bind(&demo::f, ra, _2, _1)(10,20) << endl;
	cout << bind(&demo::f, p, _1, _2)(10,20) << endl;

	struct point {
		int x;
		int y;
	};

	vector<int> v2(10);
	vector<point> v(10);
	transform(v.begin(), v.end(), v2.begin(), bind(&point::x, _1));
	for(auto x:v2) cout << x << "";

	//绑定函数对象
	bind(greater<int>(), _1, 10);
	bind(plus<int>(), _1, _2);
	bind(modulus<int>(), _1, 3);
	
	BOOST_AUTO(rex, bind(greater<int>(), _1, _2));//找的就是这个
	cout << rex(10, 20)<<endl;
	system("pause");
	return 0;
}

namespace test1 {

	class string {
	public:
		string(char* str) {
			m_data = str;
		}
		string(const string& str) {
			m_data = str.m_data;
		}

		~string() {
			m_data = NULL;
		}

		string& operator =(const string& other) {
		
		}
	private:
		char* m_data;
	};

}


//
//int* a[3];
//int(*a)[3];






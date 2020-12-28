#include <iostream>
#include <memory>

using namespace std;

class A
{
public:
    int a;
    A () : a(0) {}
    ~A() 
    {
        cout << "destructor A" << endl;
    }
};

class B
{
public:
    shared_ptr<A> A_ptr;
    int b;

    B () : b(0)
    {
        A_ptr = make_shared<A>();
    }
    ~B() 
    {
        cout << "destructor B" << endl;
    }
};

int main()
{
    B *b1 = new B();
    delete b1;
    return 0;
}
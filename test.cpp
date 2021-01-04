#include <iostream>
#include <memory>

using namespace std;

class A
{
public:
    int a;
    A () : a(0) {}
    virtual void func1()
    {
        cout << "class A!" << endl;
    }
    ~A() 
    {
        cout << "destructor A" << endl;
    }
};

class B : public A
{
public:
    int b;
    /* void func1()
    {
        cout << "class B!" << endl;
    } */

    virtual void func2()
    {
        cout << "class B virtual!" << endl;
    }
};

class C : public B
{
public:
    int a;
};

int main()
{
    B class1;
    C class2;
    A classA = static_cast<A>(class1);
    int **p = (int**)&classA;
    cout << p[0][2] << endl;
    cout << (&classA) << endl;
    p = (int**)&class1;
    cout << p[0][2] << endl;
    cout << (&class1) << endl;
    return 0;
}
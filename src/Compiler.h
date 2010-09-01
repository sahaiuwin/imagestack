#ifndef IMAGESTACK_COMPILER_H
#define IMAGESTACK_COMPILER_H

#include <vector>
#include "Parser.h"
#include "X64.h"
#include "header.h"

static const char *opname[] = {"ConstFloat", "ConstBool", "NoOp",
                        "VarX", "VarY", "VarT", "VarC", "VarVal", "Plus", "Minus", 
                        "Times", "Divide", "Sin", "Cos", "Tan", "Power",
                        "ASin", "ACos", "ATan", "ATan2", "Abs", "Floor", "Ceil", "Round",
                        "Exp", "Log", "Mod", "SampleHere", "Sample2D", "Sample3D",
                        "LT", "GT", "LTE", "GTE", "EQ", "NEQ",
                        "And", "Or", "Nand"};    


static const float const0123[] = {0, 1, 2, 3};

class Compiler {

    enum {ConstFloat = 0, ConstBool, NoOp, 
          VarX, VarY, VarT, VarC, VarVal, Plus, Minus, 
          Times, Divide, Sin, Cos, Tan, Power,
          ASin, ACos, ATan, ATan2, Abs, Floor, Ceil, Round,
          Exp, Log, Mod, SampleHere, Sample2D, Sample3D,
          LT, GT, LTE, GTE, EQ, NEQ,
          And, Or, Nand};
    
    enum {DepT = 1, DepY = 2, DepX = 4, DepC = 8, DepVal = 16};

    enum Type {Unknown = 0, Float, Bool, Int, Bool4, Float4, IntRange4};
    
    // One node in the intermediate representation
    class IRNode {
    public:
        // Opcode
        uint32_t op;
        
        // Data for Const ops
        float val;

        // Inputs
        vector<IRNode *> children;    
        
        // Who uses my value?
        vector<IRNode *> parents;
        
        // Which loop variables does this node depend on?
        uint32_t deps;

        // In what order is this instruction evaluated
        int32_t order;

        // What register will this node be computed in?
        signed char reg;
               
        // What level of the for loop will this node be computed at?
        // 0 is outermost, 4 is deepest
        signed char level;

        // What is the type of this expression?
        Type type;

        static IRNode *make(float v) {
            if (floatInstances[v] == NULL) 
                return (floatInstances[v] = new IRNode(v));
            return floatInstances[v];
        };

        static IRNode *make(Type t, uint32_t opcode, 
               Type t1 = Float, IRNode *child1 = NULL, 
               Type t2 = Float, IRNode *child2 = NULL, 
               Type t3 = Float, IRNode *child3 = NULL,
               Type t4 = Float, IRNode *child4 = NULL) {

            // collect the children into a vector
            vector<IRNode *> children;
            vector<Type> childTypes;
            if (child1) {
                children.push_back(child1);
                childTypes.push_back(t1);
            }
            if (child2) {
                children.push_back(child2);
                childTypes.push_back(t2);
            }
            if (child3) {
                children.push_back(child3);
                childTypes.push_back(t3);
            }
            if (child4) {
                children.push_back(child4);
                childTypes.push_back(t4);
            }
            return make(t, opcode, childTypes, children);
        }

        static IRNode *make(Type t, uint32_t opcode,
                            vector<Type> childTypes,
                            vector<IRNode *> children) {

            // type coercion
            for (size_t i = 0; i < children.size(); i++) {
                if (children[i]->type != childTypes[i]) {
                    if (childTypes[i] == Bool && children[i]->type == Float) {
                        children[i] = IRNode::make(Bool, NEQ, Float, children[i], Float, IRNode::make(0.0f));
                    } else if (childTypes[i] == Float && children[i]->type == Bool) {
                        children[i] = IRNode::make(Float, And, Bool, children[i], Float, IRNode::make(1.0f));
                    } else {
                        panic("I don't know how to do a type coercion!\n");
                    }
                } 
            }

           
            // strength reduction
            if (opcode == NoOp) {
                return children[0];
            }

            if (opcode == Times) {
                // 0*x = 0
                if (children[0]->op == ConstFloat && children[0]->val == 0.0f) {
                    return make(0);
                } 
                // x*0 = 0
                if (children[1]->op == ConstFloat && children[1]->val == 0.0f) {
                    return make(0);
                }

                // 1*x = x
                if (children[0]->op == ConstFloat && children[0]->val == 1.0f) {
                    return children[1];
                } 
                // x*1 = x
                if (children[1]->op == ConstFloat && children[1]->val == 1.0f) {
                    return children[0];
                }

                // 2*x = x+x
                if (children[0]->op == ConstFloat && children[0]->val == 2.0f) {
                    return make(t, Plus, childTypes[1], children[1], childTypes[1], children[1]);
                } 
                // x*2 = x+x
                if (children[1]->op == ConstFloat && children[1]->val == 2.0f) {
                    return make(t, Plus, childTypes[0], children[0], childTypes[0], children[0]);
                }
            }

            if (opcode == Divide && children[1]->op == ConstFloat) {
                // x/alpha = x*(1/alpha)
                return make(t, Times, childTypes[0], children[0], Float, make(1.0f/children[1]->val));
            }

            if (opcode == Power && children[1]->op == ConstFloat) {
                if (children[1]->val == 0.0f) {        // x^0 = 1
                    return make(1.0f);
                } else if (children[1]->val == 1.0f) { // x^1 = x
                    return children[0];
                } else if (children[1]->val == 2.0f) { // x^2 = x*x
                    return make(t, Times, childTypes[0], children[0], childTypes[0], children[0]);
                } else if (children[1]->val == 3.0f) { // x^3 = x*x*x
                    IRNode *squared = make(t, Times, childTypes[0], children[0], childTypes[0], children[0]);
                    return make(t, Times, t, squared, childTypes[0], children[0]);
                } else if (children[1]->val == 4.0f) { // x^4 = x*x*x*x
                    IRNode *squared = make(t, Times, childTypes[0], children[0], childTypes[0], children[0]);
                    return make(t, Times, t, squared, t, squared);                    
                } else if (children[1]->val == floorf(children[1]->val) &&
                           children[1]->val > 0) {
                    // iterated multiplication
                    int power = (int)floorf(children[1]->val);

                    // find the largest power of two less than power
                    int pow2 = 1;
                    while (pow2 < power) pow2 = pow2 << 1;
                    pow2 = pow2 >> 1;                                       
                    int remainder = power - pow2;
                    
                    printf("%d = %d + %d\n", power, pow2, remainder);

                    // make a stack x, x^2, x^4, x^8 ...
                    // and multiply the appropriate terms from it
                    vector<IRNode *> powStack;
                    powStack.push_back(children[0]);
                    IRNode *result = (power & 1) ? children[0] : NULL;
                    IRNode *last = children[0];
                    for (int i = 2; i < power; i *= 2) {
                        printf("Making %dth power\n", i);
                        last = make(t, Times, last->type, last, last->type, last);
                        powStack.push_back(last);
                        if (power & i) {
                            if (!result) result = last;
                            else {
                                result = make(t, Times,
                                              result->type, result,
                                              last->type, last);
                            }
                        }
                    }
                    return result;
                 }

                // todo: negative powers (integer)
                
            }            

            // rebalance summations
            if (opcode != Plus && opcode != Minus) {
                for (size_t i = 0; i < children.size(); i++) {
                    children[i]->rebalanceSum();
                }
            }              

            // deal with variables and loads
            if (opcode == VarX || opcode == VarY || opcode == VarT || opcode == VarVal || opcode == VarC) {
                vector<Type> a;
                vector<IRNode *> b;
                if (varInstances[opcode] == NULL)
                    return (varInstances[opcode] = new IRNode(t, opcode, a, b));
                return varInstances[opcode];
            }

            // common subexpression elimination - check if one of the
            // children already has a parent that does this op
            if (children.size() && children[0]->parents.size() ) {
                for (size_t i = 0; i < children[0]->parents.size(); i++) {
                    IRNode *candidate = children[0]->parents[i];
                    if (candidate->op != opcode) continue;
                    if (candidate->type != t) continue;
                    if (candidate->children.size() != children.size()) continue;
                    bool childrenMatch = true;
                    for (size_t j = 0; j < children.size(); j++) {
                        if (candidate->children[j] != children[j]) childrenMatch = false;
                    }
                    // it's the same op on the same children, reuse the old node
                    if (childrenMatch) return candidate;
                }
            }

            // make a new node
            return new IRNode(t, opcode, childTypes, children);
        }

        // An optimization pass done after generation
        IRNode *optimize() {
            IRNode * newNode = rebalanceSum();
            newNode->killOrphans();
            return newNode;
        }

        // kill everything
        static void clearAll() {
            IRNode::floatInstances.clear();
            IRNode::varInstances.clear();
            for (size_t i = 0; i < IRNode::allNodes.size(); i++) {
                delete IRNode::allNodes[i];
            }
            IRNode::allNodes.clear();            
        }

    protected:
        static map<float, IRNode *> floatInstances;
        static map<uint32_t, IRNode *> varInstances;
        static vector<IRNode *> allNodes;

        // Is this node marked for death
        bool marked;

        // remove nodes that do not assist in the computation of this node
        void killOrphans() {
            // mark all nodes for death
            for (size_t i = 0; i < allNodes.size(); i++) {
                allNodes[i]->marked = true;
            }

            // unmark those that are necessary for the computation of this
            markDescendents(false);

            vector<IRNode *> newAllNodes;
            map<float, IRNode *> newFloatInstances;
            map<uint32_t, IRNode *> newVarInstances;

            // save the unmarked nodes by migrating them to new data structures
            for (size_t i = 0; i < allNodes.size(); i++) {
                IRNode *n = allNodes[i];
                if (!n->marked) {
                    newAllNodes.push_back(n);
                    if (n->op == ConstFloat) {
                        newFloatInstances[n->val] = n;
                    } else if (n->op == VarX ||
                               n->op == VarY ||
                               n->op == VarT ||
                               n->op == VarC ||
                               n->op == VarVal) {
                        newVarInstances[n->op] = n;
                    }

                    // remove any marked parents
                    vector<IRNode *> newParents;
                    for (size_t j = 0; j < n->parents.size(); j++) {
                        if (!n->parents[j]->marked)
                            newParents.push_back(n->parents[j]);
                    }
                    n->parents.swap(newParents);
                }
            }

            // delete the marked nodes
            for (size_t i = 0; i < allNodes.size(); i++) {
                IRNode *n = allNodes[i];
                if (n->marked) delete n;
            }

            allNodes.swap(newAllNodes);
            floatInstances.swap(newFloatInstances);
            varInstances.swap(newVarInstances);
        }


        void markDescendents(bool newMark) {
            if (marked == newMark) return;
            for (size_t i = 0; i < children.size(); i++) {
                children[i]->markDescendents(newMark);
            }
            marked = newMark;
        }

        IRNode *rebalanceSum() {
            if (op != Plus && op != Minus) return this;

            // collect all the children
            vector<pair<IRNode *, bool> > terms;

            collectSum(terms);
            
            // sort them by level
            printf("Sorting %d terms...\n", terms.size());
            for (size_t i = 0; i < terms.size(); i++) {
                for (size_t j = i+1; j < terms.size(); j++) {
                    if (terms[i].first->level > terms[j].first->level) {
                        pair<IRNode *, bool> tmp = terms[i];
                        terms[i] = terms[j];
                        terms[j] = tmp;
                    }
                }
            }

            // collect the constant terms
            float constant = 0.0f;
            int firstNonConst = -1;
            for (size_t i = 0; i < terms.size(); i++) {
                if (terms[i].first->op == ConstFloat) {
                    if (terms[i].second) {
                        constant += terms[i].first->val;
                    } else {
                        constant -= terms[i].first->val;
                    }
                } else {
                    firstNonConst = i;
                    break;
                }
            }

            // constants only
            if (firstNonConst < 0) return make(constant);

            // remake the summation
            IRNode *t;
            bool tPos;
            if (constant != 0.0f) {
                // constant term plus other stuff
                t = make(constant);
                tPos = true;
            } else {
                // just non-constants
                t = terms[firstNonConst].first;
                tPos = terms[firstNonConst].second;
                firstNonConst++;
            }
            for (size_t i = firstNonConst; i < terms.size(); i++) {
                bool nextPos = terms[i].second;
                if (tPos == nextPos)
                    t = make(Float, Plus, Float, t, Float, terms[i].first);
                else if (tPos) // and not nextPos
                    t = make(Float, Minus, Float, t, Float, terms[i].first);
                else { // nextPos and not tPos
                    tPos = true;
                    t = make(Float, Minus, Float, terms[i].first, Float, t);
                }
                    
            }

            return t;
        }


        void collectSum(vector<pair<IRNode *, bool> > &terms, bool positive = true) {
            if (op == Plus) {
                children[0]->collectSum(terms, positive);
                children[1]->collectSum(terms, positive);
            } else if (op == Minus) {
                children[0]->collectSum(terms, positive);
                children[1]->collectSum(terms, !positive);                
            } else {
                terms.push_back(make_pair(this, positive));
            }
        }

        // TODO: rebalance product

        IRNode(float v) {
            allNodes.push_back(this);
            op = ConstFloat;
            val = v;
            deps = 0;
            reg = -1;
            level = 0;
            type = Float;
        }

        IRNode(Type t, uint32_t opcode, 
               vector<Type> childTypes,
               vector<IRNode *> child) {
            allNodes.push_back(this);

            for (size_t i = 0; i < child.size(); i++) 
                children.push_back(child[i]);

            type = t;
            deps = 0;
            op = opcode;
            if (opcode == VarX) deps |= DepX;
            else if (opcode == VarY) deps |= DepY;
            else if (opcode == VarT) deps |= DepT;
            else if (opcode == VarC) deps |= DepC;
            else if (opcode == VarVal) deps |= DepVal;

            // attach myself to these children and compute deps
            for (size_t i = 0; i < children.size(); i++) {
                IRNode *c = children[i];
                c->parents.push_back(this);
                deps |= c->deps;
            }

            reg = -1;

            // compute the level based on deps
            if (deps & DepVal ||
                deps & DepC) level = 4;
            else if (deps & DepX) level = 3;
            else if (deps & DepY) level = 2;
            else if (deps & DepT) level = 1;
            else level = 0;

        }
    };

public:

    void compileEval(AsmX64 *a, Window im, Window out, const Expression &exp) {
        // Remove the results from any previous compilation
        IRNode::clearAll();

        // Generate the intermediate representation from the AST. Also
        // does constant folding, common subexpression elimination,
        // dependency analysis (assigns deps and levels), and
        // rebalancing of summations and products.
        IRNode * root = irGenerator.generate(im, exp);

        // force it to be a float
        root = IRNode::make(Float, NoOp, Float, root);

        // Register assignment and evaluation ordering
        uint16_t clobbered[5], outputs[5];
        vector<vector<IRNode *> > ordering = doRegisterAssignment(root, clobbered, outputs);        

        // print out the assembly for inspection
        const char *dims = "tyxc";
        for (size_t l = 0; l < ordering.size(); l++) {
            if (l) {
                for (size_t k = 1; k < l; k++) putchar(' ');
                printf("for %c:\n", dims[l-1]);
            }
            for (size_t i = 0; i < ordering[l].size(); i++) {
                IRNode *next = ordering[l][i];
                for (size_t k = 0; k < l; k++) putchar(' ');
                printf("xmm%d = %s", next->reg, opname[next->op]);
                if (next->op == ConstFloat) {
                    printf(" %f\n", next->val);
                } else if (next->children.size() == 0) {
                    printf("\n");
                } else {
                    for (size_t j = 0; j < next->children.size(); j++) {
                        printf(" xmm%d", next->children[j]->reg);
                    }
                    printf("\n");
                }            
            }
            if (clobbered[l] & 0x3fff) {
                for (size_t k = 0; k < l; k++) putchar(' ');
                printf("clobbered: ");
                for (int i = 0; i < 14; i++) {
                    if (clobbered[l] & (1 << i)) printf("%d ", i);
                }
                printf("\n");
            }
            if (outputs[l]) {
                for (size_t k = 0; k < l; k++) putchar(' ');
                printf("output: ");
                for (int i = 0; i < 16; i++) {
                    if (outputs[l] & (1 << i)) printf("%d ", i);
                }
                printf("\n");
            }
        }

        AsmX64::Reg x = a->rax, y = a->rcx, 
            t = a->r8, c = a->rsi, 
            imPtr = a->rdx, tmp = a->r15,
            outPtr = a->rdi;

        // align the stack to a 16-byte boundary
        a->sub(a->rsp, 8);

        // save registers
        a->pushNonVolatiles();

        // reserve enough space for the output on the stack
        a->sub(a->rsp, im.channels*4*4);

        uint16_t inuse = 0;

        // generate constants
        compileBody(a, x, y, t, c, AsmX64::Mem(imPtr), im.channels*4, ordering[0]);
        a->mov(t, 0);
        a->label("tloop"); 

        // generate the values that don't depend on Y, X, C, or val
        compileBody(a, x, y, t, c, AsmX64::Mem(imPtr), im.channels*4, ordering[1]);        
        a->mov(y, 0);
        a->label("yloop"); 

        // compute the address of the start of this scanline
        a->mov(imPtr, im(0, 0));           
        a->mov(tmp, t); 
        a->imul(tmp, im.tstride*sizeof(float));
        a->add(imPtr, tmp);
        a->mov(tmp, y);
        a->imul(tmp, im.ystride*sizeof(float));
        a->add(imPtr, tmp);
        a->mov(outPtr, (int64_t)(sizeof(float)*(out(0, 0) - im(0, 0))));
        a->add(outPtr, imPtr);
        
        // generate the values that don't depend on X, C, or val
        compileBody(a, x, y, t, c, AsmX64::Mem(imPtr), im.channels*4, ordering[2]);        
        a->mov(x, 0);               
        a->label("xloop"); 
        
        // generate the values that don't depend on C or val
        compileBody(a, x, y, t, c, AsmX64::Mem(imPtr), im.channels*4, ordering[3]);        
        
        a->mov(c, 0);
        //a->label("cloop");                         

        for (int i = 0; i < im.channels; i++) {
            // insert code for the expression body
            compileBody(a, x, y, t, c, AsmX64::Mem(imPtr, i*4), im.channels*4, ordering[4]);
            // push the result onto the stack
            a->movaps(AsmX64::Mem(a->rsp, i*4*4), AsmX64::SSEReg(root->reg));
            a->add(c, 1);
        }

        // find five free registers
        vector<AsmX64::SSEReg> tmps;
        for (int i = 0; i < 16; i++) {
            if (outputs[0] & (1<<i)) continue;
            if (outputs[1] & (1<<i)) continue;
            if (outputs[2] & (1<<i)) continue;
            if (outputs[3] & (1<<i)) continue;            
            tmps.push_back(AsmX64::SSEReg(i));
            if (tmps.size() == 5) break;            
        }
        assert(tmps.size() == 5, 
               "Couldn't find five free registers to transpose and write the output,"
               " and I don't know how to spill to stack.\n");

        // Transpose and store a block of data. Only works for 3-channels right now.
        if (im.channels == 3) {
            a->movaps(tmps[0], AsmX64::Mem(a->rsp, 0));
            a->movaps(tmps[1], AsmX64::Mem(a->rsp, 16));
            a->movaps(tmps[2], AsmX64::Mem(a->rsp, 32));
            // tmp0 = r0, r1, r2, r3
            // tmp1 = g0, g1, g2, g3
            // tmp2 = b0, b1, b2, b3
            a->movaps(tmps[3], tmps[0]);
            // tmp3 = r0, r1, r2, r3
            a->shufps(tmps[3], tmps[2], 1, 3, 0, 2);            
            // tmp3 = r1, r3, b0, b2
            a->movaps(tmps[4], tmps[1]);
            // tmp4 = g0, g1, g2, g3
            a->shufps(tmps[4], tmps[2], 1, 3, 1, 3);
            // tmp4 = g1, g3, b1, b3
            a->shufps(tmps[0], tmps[1], 0, 2, 0, 2);
            // tmp0 = r0, r2, g0, g2
            a->movaps(tmps[1], tmps[0]);
            // tmp1 = r0, r2, g0, g2
            a->shufps(tmps[1], tmps[3], 0, 2, 2, 0);
            // tmp1 = r0, g0, b0, r1
            a->movntps(AsmX64::Mem(outPtr), tmps[1]);
            a->movaps(tmps[1], tmps[4]);
            // tmp1 = g1, g3, b1, b3            
            a->shufps(tmps[1], tmps[0], 0, 2, 1, 3);
            // tmp1 = g1, b1, r2, g2
            a->movntps(AsmX64::Mem(outPtr, 16), tmps[1]);
            a->movaps(tmps[1], tmps[3]);
            // tmp1 = r1, r3, b0, b2
            a->shufps(tmps[1], tmps[4], 3, 1, 1, 3);
            // tmp1 = b2, r3, g3, b3
            a->movntps(AsmX64::Mem(outPtr, 32), tmps[1]);
        } else {
            panic("For now I can't deal with images with channel counts other than 3\n");
        }
            
        //a->cmp(c, im.channels);
        //a->jl("cloop");

        a->add(imPtr, im.channels*4*4);
        a->add(outPtr, im.channels*4*4);
        a->add(x, 4);
        a->cmp(x, im.width);
        a->jl("xloop");
        a->add(y, 1);
        a->cmp(y, im.height);
        a->jl("yloop");            
        a->add(t, 1);
        a->cmp(t, im.frames);
        a->jl("tloop");            
        a->add(a->rsp, im.channels*4*4);
        a->popNonVolatiles();
        a->add(a->rsp, 8);
        a->ret();        
        a->saveCOFF("generated.obj");        
    }

    void compileBody(AsmX64 *a, 
                     AsmX64::Reg x, AsmX64::Reg y,
                     AsmX64::Reg t, AsmX64::Reg c, 
                     AsmX64::Mem ptr, int stride, 
                     vector<IRNode *> code) {
        
        AsmX64::SSEReg tmp2 = a->xmm14;
        AsmX64::SSEReg tmp = a->xmm15;

        for (size_t i = 0; i < code.size(); i++) {
            // extract the node, its register, and any children and their registers
            IRNode *node = code[i];
            IRNode *c1 = (node->children.size() >= 1) ? node->children[0] : NULL;
            IRNode *c2 = (node->children.size() >= 2) ? node->children[1] : NULL;
            IRNode *c3 = (node->children.size() >= 3) ? node->children[2] : NULL;
            IRNode *c4 = (node->children.size() >= 4) ? node->children[3] : NULL;
            AsmX64::Mem memRef(ptr);
            AsmX64::SSEReg dst(node->reg);
            AsmX64::SSEReg src1(c1 ? c1->reg : 0);
            AsmX64::SSEReg src2(c2 ? c2->reg : 0);
            AsmX64::SSEReg src3(c3 ? c3->reg : 0);
            AsmX64::SSEReg src4(c4 ? c4->reg : 0);

            switch(node->op) {
            case ConstFloat: 
                if (node->val == 0.0f) {
                    a->bxorps(dst, dst);
                } else {
                    a->mov(a->r15, &(node->val));
                    a->movss(dst, AsmX64::Mem(a->r15));
                    a->shufps(dst, dst, 0, 0, 0, 0);
                }
                break;
            case ConstBool:
                if (node->val == 0.0f) {
                    a->bxorps(dst, dst);                    
                } else {
                    a->cmpeqps(dst, dst);
                }
                break;
            case VarX:
                // TODO, this is not correct - we're vectorizing across X
                a->cvtsi2ss(dst, x);
                a->punpckldq(dst, dst);
                a->punpcklqdq(dst, dst);
                break;
            case VarY:
                a->cvtsi2ss(dst, y);
                a->punpckldq(dst, dst);
                a->punpcklqdq(dst, dst);
                break;
            case VarT:
                a->cvtsi2ss(dst, t);
                a->punpckldq(dst, dst);
                a->punpcklqdq(dst, dst);
                break;
            case VarC:
                a->cvtsi2ss(dst, c);
                a->punpckldq(dst, dst);
                a->punpcklqdq(dst, dst);
                break;
            case VarVal:
                memRef = ptr;                
                a->movss(dst, memRef);
                memRef.offset += stride;
                a->movss(tmp, memRef);
                a->punpckldq(dst, tmp);
                memRef.offset += stride;
                a->movss(tmp, memRef);
                memRef.offset += stride;
                a->movss(tmp2, memRef);
                a->punpckldq(tmp, tmp2);
                a->punpcklqdq(dst, tmp);
                break;
            case Plus:
                if (dst == src1)
                    a->addps(dst, src2);
                else if (dst == src2) 
                    a->addps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->addps(dst, src2);
                }
                break;
            case Minus:
                if (dst == src1) {
                    a->subps(dst, src2);
                } else if (dst == src2) {
                    a->movaps(tmp, src2);
                    a->movaps(src2, src1);
                    a->subps(src2, tmp);
                } else { 
                    a->movaps(dst, src1);
                    a->subps(dst, src2);
                }
                break;
            case Times: 
                if (dst == src1)
                    a->mulps(dst, src2);
                else if (dst == src2) 
                    a->mulps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->mulps(dst, src2);
                }
                break;
            case Divide: 
                if (dst == src1) {
                    a->divps(dst, src2);
                } else if (dst == src2) {
                    a->movaps(tmp, src2);
                    a->movaps(src2, src1);
                    a->divps(src2, tmp); 
                } else {
                    a->movaps(dst, src1);
                    a->divps(dst, src2);
                }
                break;
            case And:
                if (dst == src1) 
                    a->bandps(dst, src2);
                else if (dst == src2)
                    a->bandps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->bandps(dst, src2);
                }
                break;
            case Nand:
                if (dst == src1) {
                    a->bandnps(dst, src2);
                } else if (dst == src2) {
                    a->movaps(tmp, src2);
                    a->movaps(src2, src1);
                    a->bandnps(src2, tmp); 
                } else {
                    a->movaps(dst, src1);
                    a->bandnps(dst, src2);
                }
                break;
            case Or:               
                if (dst == src1) 
                    a->borps(dst, src2);
                else if (dst == src2)
                    a->borps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->borps(dst, src2);
                }
                break;
            case NEQ:                               
                if (dst == src1) 
                    a->cmpneqps(dst, src2);
                else if (dst == src2)
                    a->cmpneqps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->cmpneqps(dst, src2);
                }
                break;
            case EQ:
                if (dst == src1) 
                    a->cmpeqps(dst, src2);
                else if (dst == src2)
                    a->cmpeqps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->cmpeqps(dst, src2);
                }
                break;
            case LT:
                if (dst == src1) 
                    a->cmpltps(dst, src2);
                else if (dst == src2)
                    a->cmpnleps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->cmpltps(dst, src2);
                }
                break;
            case GT:
                if (dst == src1) 
                    a->cmpnleps(dst, src2);
                else if (dst == src2)
                    a->cmpltps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->cmpnleps(dst, src2);
                }
                break;
            case LTE:
                if (dst == src1) 
                    a->cmpleps(dst, src2);
                else if (dst == src2)
                    a->cmpnltps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->cmpleps(dst, src2);
                }
                break;
            case GTE:
                if (dst == src1) 
                    a->cmpnltps(dst, src2);
                else if (dst == src2)
                    a->cmpleps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->cmpnltps(dst, src2);
                }
                break;
            case ATan2:
            case Mod:
            case Power:
            case Sin:
            case Cos:
            case Tan:
            case ASin:
            case ACos:
            case ATan:
            case Exp:
            case Log:
            case Floor:
            case Ceil:
            case Round:
            case Abs:               
            case Sample2D: 
            case Sample3D:
            case SampleHere:
                printf("Not implemented: %s\n", opname[node->op]);                
                break;
            case NoOp:
                break;
            }
        }
    }

protected:

    vector<vector<IRNode *> > doRegisterAssignment(IRNode *root, uint16_t clobbered[5], uint16_t output[5]) {
        // reserve xmm14-15 for the code generator
        vector<IRNode *> regs(14);
        
        // the resulting evaluation order
        vector<vector<IRNode *> > order(5);
        regAssign(root, regs, order);

        // detect what registers get clobbered
        for (int i = 0; i < 5; i++) {
            clobbered[i] = (1<<15) | (1<<14);
            for (size_t j = 0; j < order[i].size(); j++) {
                IRNode *node = order[i][j];
                clobbered[i] |= (1 << node->reg);
            }
        }

        // detect what registers are used for inter-level communication
        output[0] = 0;
        for (int i = 1; i < 5; i++) {
            output[i] = 0;
            for (size_t j = 0; j < order[i].size(); j++) {
                IRNode *node = order[i][j];
                for (size_t k = 0; k < node->children.size(); k++) {
                    IRNode *child = node->children[k];
                    if (child->level != node->level) {
                        output[child->level] |= (1 << child->reg);
                    }
                }
            }
        }
        output[4] = (1 << root->reg);

        return order;        
    }
       
    void regAssign(IRNode *node, vector<IRNode *> &regs, vector<vector<IRNode *> > &order) {
        // if I already have a register bail out
        if (node->reg >= 0) return;

        // assign registers to the children
        for (size_t i = 0; i < node->children.size(); i++) {
            regAssign(node->children[i], regs, order);
        }

        // if there are children, see if we can use the register of
        // the one of the children - the first is optimal, as this
        // makes x64 codegen easier. To reuse the register of the
        // child it has to be at the same level as us (otherwise it
        // will have been computed once and stored outside the for
        // loop this node lives in), and have no other
        // parents. There's an exception to this: if you're the last
        // parent of a set of parents to use a child's value, it's OK
        // to clobber. We currently don't handle this exception
        // (because we currently don't do CSE, so it never crops up).

        if (node->children.size()) {
            IRNode *child1 = node->children[0];
            bool okToClobber = true;
            // must be the same level
            if (node->level != child1->level) okToClobber = false;
            // every parent must be this, or at the same level and already evaluated
            for (size_t i = 0; i < child1->parents.size() && okToClobber; i++) {
                if (child1->parents[i] != node && 
                    (child1->parents[i]->level != node->level ||
                     child1->parents[i]->reg < 0)) {
                    okToClobber = false;
                }
            }
            if (okToClobber) {
                node->reg = child1->reg;
                regs[child1->reg] = node;
                node->order = order[node->level].size();
                order[node->level].push_back(node);
                return;
            }
        }
        
        // Some binary ops are easy to flip, so we should try to clobber the second child
        if (node->op == And ||
            node->op == Or ||
            node->op == Plus ||
            node->op == Times ||
            node->op == LT ||
            node->op == GT ||
            node->op == LTE ||
            node->op == GTE ||
            node->op == EQ ||
            node->op == NEQ) {

            IRNode *child2 = node->children[1];
            bool okToClobber = true;
            // must be the same level
            if (node->level != child2->level) okToClobber = false;
            // every parent must be this, or at the same level and already evaluated
            for (size_t i = 0; i < child2->parents.size() && okToClobber; i++) {
                if (child2->parents[i] != node && 
                    (child2->parents[i]->level != node->level ||
                     child2->parents[i]->reg < 0)) {
                    okToClobber = false;
                }
            }
            if (okToClobber) {
                node->reg = child2->reg;
                regs[child2->reg] = node;
                node->order = order[node->level].size();
                order[node->level].push_back(node);
                return;
            }
        }

        // else find a previously used register that is safe to evict
        // - meaning it's at the same or higher level and all its
        // parents will have already been evaluated and are at the same or higher level
        for (size_t i = 0; i < regs.size(); i++) {            
            // don't consider unused registers yet
            if (!regs[i]) continue;

            // don't clobber registers from a higher level
            if (regs[i]->level < node->level) continue;

            // only clobber registers whose parents will have been fully evaluated
            bool safeToEvict = true;            
            for (size_t j = 0; j < regs[i]->parents.size(); j++) {
                if (regs[i]->parents[j]->reg < 0 ||
                    regs[i]->parents[j]->level > node->level) {
                    safeToEvict = false;
                    break;
                }
            }

            if (safeToEvict) {
                node->reg = i;
                regs[i] = node;
                node->order = order[node->level].size();
                order[node->level].push_back(node);
                return;
            }
        }

        // else find a completely unused register and use that. 
        for (size_t i = 0; i < regs.size(); i++) {
            if (regs[i] == NULL) {
                node->reg = i;
                regs[i] = node;
                node->order = order[node->level].size();
                order[node->level].push_back(node);
                return;
            }
        }

        // else clobber a non-primary child. This sometimes requires
        // two movs, so it's the least favored option
        for (size_t i = 1; i < node->children.size(); i++) {
            IRNode *child = node->children[i];
            if (node->level == child->level &&
                child->parents.size() == 1) {
                node->reg = child->reg;
                regs[child->reg] = node;
                node->order = order[node->level].size();
                order[node->level].push_back(node);
                return;
            }
        }

        // else freak out - we're out of registers and we don't know
        // how to spill to the stack yet.
        panic("Out of registers!\n");
    }

    
    // This class traverses the expression tree generating IR
    // Nodes. It's sole state is the resultant root node of the
    // expression.
    class IRGenerator : public Expression::Visitor {
    protected:
        IRNode *root;
        Stats *stats;
        Window im;

        IRNode *descend(Expression::Node *node) {
            node->accept(this);
            return root;
        }

    public:
        IRNode *generate(Window w, const Expression &exp) {
            im = w;
            stats = new Stats(w);
            root = descend(exp.root);
            delete stats;
            root = root->optimize();
            return root;
        }

        

#define nullary(a, b)                             \
        void visit(Expression::a *node) {         \
            root = IRNode::make(Float, b);        \
        }
        
        nullary(Var_x, VarX);
        nullary(Var_y, VarY);
        nullary(Var_t, VarT);
        nullary(Var_c, VarC);
        nullary(Var_val, VarVal);
        
#undef nullary
        
#define constFloat(a, b)                      \
        void visit(Expression::a *node) {         \
            root = IRNode::make(b);               \
        }
        
        constFloat(Float, node->value);
        constFloat(Uniform_width, im.width);
        constFloat(Uniform_height, im.height);
        constFloat(Uniform_frames, im.frames);
        constFloat(Uniform_channels, im.channels);
        constFloat(Funct_mean0, stats->mean());
        constFloat(Funct_sum0, stats->sum());
        constFloat(Funct_min0, stats->minimum());
        constFloat(Funct_max0, stats->maximum());
        constFloat(Funct_stddev0, sqrtf(stats->variance()));
        constFloat(Funct_variance0, stats->variance());
        constFloat(Funct_skew0, stats->skew());
        constFloat(Funct_kurtosis0, stats->kurtosis());
        
        // TODO
        constFloat(Funct_mean1, stats->mean());
        constFloat(Funct_sum1, stats->sum());
        constFloat(Funct_min1, stats->minimum());
        constFloat(Funct_max1, stats->maximum());
        constFloat(Funct_stddev1, sqrtf(stats->variance()));
        constFloat(Funct_variance1, stats->variance());
        constFloat(Funct_skew1, stats->skew());
        constFloat(Funct_kurtosis1, stats->kurtosis());    
        constFloat(Funct_covariance, stats->variance());    
        
#undef constFloat
        
        // roundf is a macro, which gets confused inside another macro, so
        // we wrap it in a function
        float myRound(float x) {
            return roundf(x);
        }
        
        // -x gets compiled to (0-x)
        void visit(Expression::Negation *node) {
            IRNode *child1 = descend(node->arg1);
            if (child1->op == ConstFloat) {            
                root = IRNode::make(-child1->val);
            } else {
                root = IRNode::make(Float, Minus, Float, IRNode::make(0.0f), Float, root);
            }
        }
        
#define unary(a, b, c)                                            \
        void visit(Expression::a *node) {                         \
            IRNode *child1 = descend(node->arg1);                 \
            if (child1->op == ConstFloat) {                       \
                root = IRNode::make(b(child1->val));              \
            } else {                                              \
                root = IRNode::make(Float, c, Float, root);       \
            }                                                     \
        }
        
        unary(Funct_sin, sinf, Sin);
        unary(Funct_cos, cosf, Cos);
        unary(Funct_tan, tanf, Tan);
        unary(Funct_asin, asinf, ASin);
        unary(Funct_acos, acosf, ACos);
        unary(Funct_atan, atanf, ATan);
        unary(Funct_abs, fabs, Abs);
        unary(Funct_floor, floorf, Floor);
        unary(Funct_ceil, ceilf, Ceil);
        unary(Funct_round, myRound, Round);
        unary(Funct_log, logf, Log);
        unary(Funct_exp, expf, Exp);
        
#undef unary
        
// TODO: boolean eq and neq
#define bincmp(a, b)                                                    \
        void visit(Expression::a *node) {                               \
            IRNode *child1 = descend(node->arg1);                       \
            IRNode *child2 = descend(node->arg2);                       \
            if (child1->op == ConstFloat && child2->op == ConstFloat) { \
                root = IRNode::make((child1->val b child2->val) ? 1.0f : 0.0f); \
                root->type = Bool;                                      \
            } else {                                                    \
                root = IRNode::make(Bool, a, Float, child1, Float, child2); \
            }                                                           \
        } 
        
        
        bincmp(LTE, <=);
        bincmp(LT, <);
        bincmp(GT, >);
        bincmp(GTE, >=);
        bincmp(EQ, ==);
        bincmp(NEQ, !=);
        
#undef bincmp

// TODO: bool * bool = bool
#define binop(a, b)                                                     \
        void visit(Expression::a *node) {                               \
            IRNode *child1 = descend(node->arg1);                       \
            IRNode *child2 = descend(node->arg2);                       \
            if (child1->op == ConstFloat && child2->op == ConstFloat) { \
                root = IRNode::make(child1->val b child2->val);         \
            } else {                                                    \
                root = IRNode::make(Float, a, Float, child1, Float, child2); \
            }                                                           \
        } 
        
        binop(Plus, +);
        binop(Minus, -);
        binop(Times, *);
        binop(Divide, /);
        
#undef binop
        
#define binfunc(a, b, c)                                                \
        void visit(Expression::a *node) {                               \
            IRNode *child1 = descend(node->arg1);                       \
            IRNode *child2 = descend(node->arg2);                       \
            if (child1->op == ConstFloat && child2->op == ConstFloat) { \
                root = IRNode::make(b(child1->val, child2->val));       \
            } else {                                                    \
                root = IRNode::make(Float, c, Float, child1, Float, child2); \
            }                                                           \
        }
        
        
        binfunc(Mod, fmod, Mod);
        binfunc(Funct_atan2, atan2f, ATan2);
        binfunc(Power, powf, Power);
        
#undef binfunc
        
        void visit(Expression::IfThenElse *node) {
            IRNode *cond = descend(node->arg1);
            
            if (cond->op == ConstBool || cond->op == ConstFloat) {
                if (cond->val) {
                    root = descend(node->arg2);
                    return;
                } else { 
                    root = descend(node->arg3);
                    return;
                }            
            }
            
            IRNode *thenCase = descend(node->arg2);
            IRNode *elseCase = descend(node->arg3);
            IRNode *left = IRNode::make(thenCase->type, And, Bool, cond, thenCase->type, thenCase);
            IRNode *right = IRNode::make(elseCase->type, Nand, Bool, cond, elseCase->type, elseCase);
            Type t = Float;
            if (thenCase->type == Bool && elseCase->type == Bool) 
                t = Bool;
            root = IRNode::make(t, Or, left->type, left, right->type, right);
            
        }
        
        void visit(Expression::SampleHere *node) {        
            IRNode *child1 = descend(node->arg1);
            root = IRNode::make(Float, SampleHere, Float, child1);
        }
        
        void visit(Expression::Sample2D *node) {
            IRNode *child1 = descend(node->arg1);                       
            IRNode *child2 = descend(node->arg2);                       
            root = IRNode::make(Float, Sample2D, Float, child1, Float, child2);
        }
        
        void visit(Expression::Sample3D *node) {
            IRNode *child1 = descend(node->arg1);                       
            IRNode *child2 = descend(node->arg2);                       
            IRNode *child3 = descend(node->arg3);
            root = IRNode::make(Float, Sample3D, Float, child1, Float, child2, Float, child3);
        }
    } irGenerator;    
};

#include "footer.h"
#endif

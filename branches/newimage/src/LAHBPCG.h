#ifndef LAHBPCG_H
#define LAHBPCG_H
#include "header.h"

class LAHBPCG : public Operation {
public:
    void help();
    void parse(vector<string> args);

    static NewImage apply(NewImage d, NewImage gx, NewImage gy, 
			  NewImage w, NewImage sx, NewImage sy, int max_iter, float tol);
private:
};

#include "footer.h"
#endif

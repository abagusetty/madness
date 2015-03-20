/*
  This file is part of MADNESS.

  Copyright (C) 2007,2010 Oak Ridge National Laboratory

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

  For more information please contact:

  Robert J. Harrison
  Oak Ridge National Laboratory
  One Bethel Valley Road
  P.O. Box 2008, MS-6367

  email: harrisonrj@ornl.gov
  tel:   865-241-3937
  fax:   865-572-0680

*/


/// \file chem/molecular_optimizer.h
/// \brief optimize the geometrical structure of a molecule
#ifndef MADNESS_CHEM_MOLECULAR_OPTIMIZER_H__INCLUDED
#define MADNESS_CHEM_MOLECULAR_OPTIMIZER_H__INCLUDED

#include <madness/tensor/solvers.h>
#include <chem/molecule.h>

namespace madness {


struct MolecularOptimizationTargetInterface : public OptimizationTargetInterface {

    /// return the molecule of the target
    virtual Molecule& molecule() {
        MADNESS_EXCEPTION("you need to return a molecule",1);
        return *(new Molecule());   // this is a memory leak silencing warnings
    }
};



/// Molecular optimizer derived from the QuasiNewton optimizer

/// Essentially the QuasiNewton optimizer, but with the additional feature
/// of projecting out rotational and translational degrees of freedom
class MolecularOptimizer : public OptimizerInterface {

public:
    /// same ctor as the QuasiNewton optimizer
    MolecularOptimizer(const std::shared_ptr<MolecularOptimizationTargetInterface>& tar,
            int maxiter = 20, double tol = 1e-6, double value_precision = 1e-12,
            double gradient_precision = 1e-12)
        : update("BFGS")
        , target(tar)
        , maxiter(maxiter)
        , tol(tol)
        , value_precision(value_precision)
        , gradient_precision(gradient_precision)
        , f(tol*1e16)
        , gnorm(tol*1e16)
        , printtest(false)
        , cg_method("polak_ribiere") {
    }

    /// optimize the underlying molecule

    /// @param[in]  x   the coordinates to compute energy and gradient
    bool optimize(Tensor<double>& x) {
        bool converge;
        converge=optimize_quasi_newton(x);
//        converge=optimize_conjugate_gradients(x);
        return converge;
    }

    bool converged() const {return gradient_norm()<tol;}

    double value() const {return 0.0;}

    double gradient_norm() const {return gnorm;}

private:

    /// How to update the hessian: BFGS or SR1
    std::string update;
    std::shared_ptr<MolecularOptimizationTargetInterface> target;
    const int maxiter;
    const double tol;       // the gradient convergence threshold
    const double value_precision;  // Numerical precision of value
    const double gradient_precision; // Numerical precision of each element of residual
    double f;
    double gnorm;
    Tensor<double> h;
    bool printtest;

    /// conjugate_gradients method
    std::string cg_method;

    bool optimize_quasi_newton(Tensor<double>& x) {

        if(printtest)  target->test_gradient(x, value_precision);

        bool h_is_identity = (h.size() == 0);
        if (h_is_identity) {
            int n = x.dim(0);
            h = Tensor<double>(n,n);
            for (int i=0; i<n; ++i) h(i,i) = 1.0;

            // mass-weight the initial hessian
            for (int i=0; i<target->molecule().natom(); ++i) {
                h(i  ,i  )/=(target->molecule().get_atom(i).mass);
                h(i+1,i+1)/=(target->molecule().get_atom(i).mass);
                h(i+2,i+2)/=(target->molecule().get_atom(i).mass);
            }
        }

        remove_external_dof(h,target->molecule());

        // the previous gradient
        Tensor<double> gp;
        // the displacement
        Tensor<double> dx;

        for (int iter=0; iter<maxiter; ++iter) {
            Tensor<double> gradient;

            target->value_and_gradient(x, f, gradient);
            print("gopt: new energy",f);
            gnorm = gradient.normf()/sqrt(gradient.size());
            print("gopt: raw gradient norm ",gnorm);

            // remove external degrees of freedom (translation and rotation)
            Tensor<double> project_ext=projector_external_dof(target->molecule());
            gradient=inner(gradient,project_ext);
            gnorm = gradient.normf()/sqrt(gradient.size());
            print("gopt: projected gradient norm ",gnorm);


            printf(" QuasiNewton iteration %2d value %.12e gradient %.2e\n",iter,f,gnorm);
            if (converged()) break;

            if (iter == 1 && h_is_identity) {
                // Default initial Hessian is scaled identity but
                // prefer to reuse any existing approximation.
                h.scale(gradient.trace(gp)/gp.trace(dx));
            }

            if (iter > 0) {
                if (update == "BFGS") QuasiNewton::hessian_update_bfgs(dx, gradient-gp,h);
                else QuasiNewton::hessian_update_sr1(dx, gradient-gp,h);
            }

            Tensor<double> v, e;
//            syev(h, v, e);
//            print("hessian eigenvalues",e);

            remove_external_dof(h,target->molecule());
            syev(h, v, e);
            print("hessian eigenvalues",e);

            // this will invert the hessian, multiply with the gradient and
            // return the displacements
            dx = new_search_direction2(gradient,h);

//            double step = line_search(1.0, f, dx.trace(g), x, dx);
            double step=0.5;

            dx.scale(step);
            x += dx;
            gp = gradient;
        }

        if (printtest) {
            print("final hessian");
            print(h);
        }
        return converged();
    }

    bool optimize_conjugate_gradients(Tensor<double>& x) {

//        Tensor<double> project_ext=projector_external_dof(target->molecule());


        // initial energy and gradient gradient
        double energy=0.0;
        Tensor<double> gradient;

        // first step is steepest descent
        Tensor<double> displacement(x.size());
        Tensor<double> oldgradient;
        Tensor<double> old_displacement(x.size());
        old_displacement.fill(0.0);

        for (int iter=1; iter<maxiter; ++iter) {

            // displace coordinates
            if (iter>1) x+=displacement;

            // compute energy and gradient
            target->value_and_gradient(x, energy, gradient);
            print("gopt: new energy",energy);
            gnorm = gradient.normf()/sqrt(gradient.size());
            print("gopt: raw gradient norm ",gnorm);

            // remove external degrees of freedom (translation and rotation)
            Tensor<double> project_ext=projector_external_dof(target->molecule());
            gradient=inner(gradient,project_ext);
            gnorm = gradient.normf()/sqrt(gradient.size());
            print("gopt: projected gradient norm ",gnorm);

            // compute new displacement
            if (iter==1) {
                displacement=-gradient;
            } else {
                double beta=0.0;
                if (cg_method=="fletcher_reeves")
                    beta=gradient.normf()/oldgradient.normf();
                if (cg_method=="polak_ribiere")
                    beta=gradient.normf()/(gradient-oldgradient).normf();
                displacement=-gradient + beta * old_displacement;
            }

            // save displacement for the next step
            old_displacement=displacement;

            if (converged() and (displacement.normf()/displacement.size()<tol)) break;
        }

        return converged();
    }

    /// effectively invert the hessian and multiply with the gradient
    Tensor<double> new_search_direction2(const Tensor<double>& g,
            const Tensor<double>& hessian) const {
        Tensor<double> dx, s;
        double tol = gradient_precision;
        double trust = 1.0; // This applied in spectral basis

        // diagonalize the hessian:
        // VT H V = lambda
        // H^-1   = V lambda^-1 VT
        Tensor<double> v, e;
        syev(hessian, v, e);

        // Transform gradient into spectral basis
        // H^-1 g = V lambda^-1 VT g
        Tensor<double> gv = inner(g,v); // this is VT g == gT V == gv

        // Take step applying restriction
        int nneg=0, nsmall=0, nrestrict=0;
        for (int i=0; i<e.size(); ++i) {
            if (e[i] < -tol) {
                if (printtest) printf(
                        "   forcing negative eigenvalue to be positive %d %.1e\n", i, e[i]);
                nneg++;
                //e[i] = -2.0*e[i]; // Enforce positive search direction
                e[i] = -0.1*e[i]; // Enforce positive search direction
            }
            else if (e[i] < tol) {
                if (printtest) printf(
                        "   forcing small eigenvalue to be zero %d %.1e\n", i, e[i]);
                nsmall++;
                e[i] = tol;
                gv[i]=0.0;   // effectively removing this direction
            }

            // this is the step -lambda^-1 gv
            gv[i] = -gv[i] / e[i];
            if (std::abs(gv[i]) > trust) { // Step restriction
                double gvnew = trust*std::abs(gv(i))/gv[i];
                if (printtest) printf(
                        "   restricting step in spectral direction %d %.1e --> %.1e\n",
                        i, gv[i], gvnew);
                nrestrict++;
                gv[i] = gvnew;
            }
        }
        if (nneg || nsmall || nrestrict)
            printf("   nneg=%d nsmall=%d nrestrict=%d\n", nneg, nsmall, nrestrict);

        // Transform back from spectral basis to give the displacements
        // disp = -V lambda^-1 VT g = V lambda^-1 gv
        return inner(v,gv);
    }

    /// compute the projector to remove transl. and rot. degrees of freedom

    /// taken from http://www.gaussian.com/g_whitepap/vib.htm
    /// I don't really understand the concept behind the projectors, but it
    /// seems to work, and it is not written down explicitly anywhere!
    /// All quantities are computed in non-mass-weighted coordinates.
    static Tensor<double> projector_external_dof(Molecule& mol) {

        // compute the translation vectors
        Tensor<double> transx(3*mol.natom());
        Tensor<double> transy(3*mol.natom());
        Tensor<double> transz(3*mol.natom());
        for (int i=0; i<3*mol.natom(); i+=3) {
            transx[i]=1.0/sqrt(mol.natom());
            transy[i+1]=1.0/sqrt(mol.natom());
            transz[i+2]=1.0/sqrt(mol.natom());
        }

        // compute the rotation vectors

        // move the molecule to its center of mass and compute
        // the moment of inertia tensor
        Tensor<double> com=mol.center_of_mass();
        mol.translate(-1.0*com);
        Tensor<double> I=mol.moment_of_inertia();
        mol.translate(1.0*com);

        // diagonalize the moment of inertia
        Tensor<double> v,e;
        syev(I, v, e);  // v being the "X" tensor on the web site

        // rotation vectors
        Tensor<double> rotx(3*mol.natom());
        Tensor<double> roty(3*mol.natom());
        Tensor<double> rotz(3*mol.natom());

        for (int iatom=0; iatom<mol.natom(); ++iatom) {

            // coordinates wrt the center of mass ("R" on the web site)
            Tensor<double> coord(3);
            coord(0l)=mol.get_atom(iatom).x-com(0l);
            coord(1l)=mol.get_atom(iatom).y-com(1l);
            coord(2l)=mol.get_atom(iatom).z-com(2l);

            // p is the dot product of R and X on the web site
            Tensor<double> p=inner(coord,v);

            // Eq. (5)
            rotx(3*iatom + 0)=p(1)*v(0,2)-p(2)*v(0,1);
            rotx(3*iatom + 1)=p(1)*v(1,2)-p(2)*v(1,1);
            rotx(3*iatom + 2)=p(1)*v(2,2)-p(2)*v(2,1);

            roty(3*iatom + 0)=p(2)*v(0,0)-p(0l)*v(0,2);
            roty(3*iatom + 1)=p(2)*v(1,0)-p(0l)*v(1,2);
            roty(3*iatom + 2)=p(2)*v(2,0)-p(0l)*v(2,2);

            rotz(3*iatom + 0)=p(0l)*v(0,1)-p(1)*v(0,0);
            rotz(3*iatom + 1)=p(0l)*v(1,1)-p(1)*v(1,0);
            rotz(3*iatom + 2)=p(0l)*v(2,1)-p(1)*v(2,0);

        }

        // move the translational and rotational vectors to a common tensor
        Tensor<double> ext_dof(6,3*mol.natom());
        ext_dof(0l,_)=rotx;
        ext_dof(1l,_)=roty;
        ext_dof(2l,_)=rotz;
        ext_dof(3l,_)=transx;
        ext_dof(4l,_)=transy;
        ext_dof(5l,_)=transz;

        // compute overlap to orthonormalize the projectors
        Tensor<double> ovlp=inner(ext_dof,ext_dof,1,1);
        syev(ovlp,v,e);
        // orthogonalize with the eigenvectors of ovlp
        ext_dof=inner(v,ext_dof,0,0);

        // normalize or remove the dof if necessary (e.g. linear molecules)
        for (int i=0; i<6; ++i) {
            if (e(i)<1.e-14) {
                ext_dof(i,_).scale(0.0);      // take out this degree of freedom
            } else {
                ext_dof(i,_).scale(1/sqrt(e(i)));   // normalize
            }
        }

        // construct projector on the complement of the rotations
        Tensor<double> projector(3*mol.natom(),3*mol.natom());
        for (int i=0; i<3*mol.natom(); ++i) projector(i,i)=1.0;

        // compute the outer products of the projectors
        // 1- \sum_i | t_i >< t_i |
        projector-=inner(ext_dof,ext_dof,0,0);
        return projector;

    }

public:
    /// remove translational degrees of freedom from the hessian
    static void remove_external_dof(Tensor<double>& hessian,
            Molecule& mol) {

        print("projecting out translational and rotational degrees of freedom");
        // compute the translation of the center of mass
        Tensor<double> projector_ext=projector_external_dof(mol);

        // this is P^T * H * P
        hessian=inner(projector_ext,inner(hessian,projector_ext),0,0);
    }



};

}

#endif //MADNESS_CHEM_MOLECULAR_OPTIMIZER_H__INCLUDED

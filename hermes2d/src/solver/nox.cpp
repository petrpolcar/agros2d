// This file is part of Hermes2D
//
// Hermes2D is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// Hermes2D is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Hermes2D.  If not, see <http://www.gnu.org/licenses/>.

#include "../config.h"
#include "nox.h"

#define NOX_NOT_COMPILED "hermes2d was not built with NOX support."

#ifdef HAVE_NOX

static Epetra_SerialComm seq_comm;

// NOX Problem Interface ///////////////////////////////////////////////////////////////////////////

/// A helper for NOX solver
///
/// @ingroup solvers
class NoxProblemInterface :
	public NOX::Epetra::Interface::Required,
	public NOX::Epetra::Interface::Jacobian,
	public NOX::Epetra::Interface::Preconditioner
{
public:
	NoxProblemInterface(FeProblem* problem);
	virtual ~NoxProblemInterface();

	/// Compute and return F
	virtual bool computeF(const Epetra_Vector &x, Epetra_Vector &f, FillType flag = Residual);
	/// Compute an explicit Jacobian
	virtual bool computeJacobian(const Epetra_Vector &x, Epetra_Operator &op);

	/// Computes a user supplied preconditioner based on input vector x.
	/// @return true if computation was successful.
	virtual bool computePreconditioner(const Epetra_Vector &x, Epetra_Operator &m,
	                                   Teuchos::ParameterList *precParams = 0);

	// Return a reference to the Epetra_CrsMatrix with the Jacobian
	// that is generated by the Interface class.
	EpetraMatrix *get_jacobian() { return &jacobian; }

	// Return a reference to the Epetra_Vector with the initial guess
	// that is generated by the Interface class.
	EpetraVector *get_init_sln() { return &init_sln; }

	void set_init_sln(double *ic);

	/// Accessor for preconditioner
    Teuchos::RCP<Precond> get_precond() { return precond; }
	void set_precond(Teuchos::RCP<Precond> &pc);

	FeProblem* fep;			// finite element problem being solved

	EpetraVector init_sln;		// initial solution
	EpetraMatrix jacobian;		// jacobian (optional)
    Teuchos::RCP<Precond> precond;		// preconditiner (optional)

	void prealloc_jacobian();
};

NoxProblemInterface::NoxProblemInterface(FeProblem* problem)
{
  fep = problem;
  int ndof = fep->get_num_dofs();
  // allocate initial solution
  init_sln.alloc(ndof);
  if (!fep->is_matrix_free()) prealloc_jacobian();

  this->precond = Teuchos::null;
}

NoxProblemInterface::~NoxProblemInterface()
{
}

void NoxProblemInterface::prealloc_jacobian()
{
  // preallocate jacobian structure
  fep->create(&jacobian);
  jacobian.finish();
}

void NoxProblemInterface::set_precond(Teuchos::RCP<Precond> &pc)
{
  precond = pc;
  prealloc_jacobian();
}

void NoxProblemInterface::set_init_sln(double *ic)
{
  int size = fep->get_num_dofs();
  int *idx = new int[size];
  for (int i = 0; i < size; i++) init_sln.set(i, ic[i]);
  delete [] idx;
}

bool NoxProblemInterface::computeF(const Epetra_Vector &x, Epetra_Vector &f, FillType flag)
{
  EpetraVector xx(x);  // wrap our structures around core Epetra objects
  EpetraVector rhs(f);

  rhs.zero();
  // The first NULL is for the global matrix.
  scalar* coeff_vec;
  xx.extract(coeff_vec);
  fep->assemble(coeff_vec, NULL, &rhs);

  return true;
}

bool NoxProblemInterface::computeJacobian(const Epetra_Vector &x, Epetra_Operator &op)
{
  Epetra_RowMatrix *jac = dynamic_cast<Epetra_RowMatrix *>(&op);
  assert(jac != NULL);

  EpetraVector xx(x);			// wrap our structures around core Epetra objects
  EpetraMatrix jacobian(*jac);

  jacobian.zero();
  // The first NULL is for the right-hand side.
  scalar* coeff_vec;
  xx.extract(coeff_vec);
  fep->assemble(coeff_vec, &jacobian, NULL);
  jacobian.finish();

  return true;
}

/// Computes a user supplied preconditioner based on input vector x.
/// @return true if computation was successful.
bool NoxProblemInterface::computePreconditioner(const Epetra_Vector &x, Epetra_Operator &m,
                                                Teuchos::ParameterList *precParams)
{
  assert(precond != Teuchos::null);
  EpetraVector xx(x);			// wrap our structures around core Epetra objects

  jacobian.zero();
  // The first NULL is for the right-hand side.
  scalar* coeff_vec;
  xx.extract(coeff_vec);
  fep->assemble(coeff_vec, &jacobian, NULL);
  jacobian.finish();

  precond->create(&jacobian);
  precond->compute();
  m = *precond->get_obj();

  return true;
}

#endif

// NOX solver //////////////////////////////////////////////////////////////////////////////////////

NoxSolver::NoxSolver(FeProblem* problem)
{
#ifdef HAVE_NOX
  // default values
  nl_dir = "Newton";
  output_flags = NOX::Utils::Error;
  // linear solver settings
  ls_type = "GMRES";
  ls_max_iters = 800;
  ls_tolerance = 1e-8;
  ls_sizeof_krylov_subspace = 50;
  precond_yes = false;
  precond_type = "None";
  // convergence test
  conv.max_iters = 10;
  conv.abs_resid = 1.0e-6;
  conv.rel_resid = 1.0e-2;
  conv.norm_type = NOX::Abstract::Vector::TwoNorm;
  conv.stype = NOX::StatusTest::NormF::Scaled;
  conv.update = 1.0e-5;
  conv.wrms_rtol = 1.0e-2;
  conv.wrms_atol = 1.0e-8;

  conv_flag.absresid = 1;
  conv_flag.relresid = 0;
  conv_flag.update = 0;
  conv_flag.wrms = 0;

  // Create the interface between the test problem and the nonlinear solver
  // This is created by the user using inheritance of the abstract base class:
  // NOX_Epetra_Interface
  interface = Teuchos::rcp(new NoxProblemInterface(problem));
#else
  error(NOX_NOT_COMPILED);
#endif
}

NoxSolver::~NoxSolver()
{
#ifdef HAVE_NOX
  // FIXME: this does not destroy the "interface", and Trilinos 
  // complains at closing main.cpp.
  interface->fep->invalidate_matrix();
#endif
}

#ifdef HAVE_TEUCHOS
void NoxSolver::set_precond(Teuchos::RCP<Precond> &pc)
{
#ifdef HAVE_NOX
  precond_yes = true;
  interface->set_precond(pc);
#endif
}
#endif

void NoxSolver::set_precond(const char *pc)
{
#ifdef HAVE_NOX
  precond_yes = true;
  precond_type = pc;
#endif
}

bool NoxSolver::set_init_sln(double *ic)
{
#ifdef HAVE_NOX
	interface->set_init_sln(ic);
	return true;
#else
	return false;
#endif
}

bool NoxSolver::set_init_sln(EpetraVector *ic)
{
#ifdef HAVE_NOX
	double *vals;
	ic->vec->ExtractView(&vals);
	interface->set_init_sln(vals);
	return true;
#else
	return false;
#endif
}

bool NoxSolver::solve()
{
#ifdef HAVE_NOX
   if (interface->fep->get_num_dofs() == 0) return false;

   // start from the initial solution
   NOX::Epetra::Vector nox_sln_vec(*interface->get_init_sln()->vec);

   // Create the top level parameter list
   Teuchos::RCP<Teuchos::ParameterList> nl_pars_ptr = Teuchos::rcp(new Teuchos::ParameterList);
   Teuchos::ParameterList &nl_pars = *nl_pars_ptr.get();

   // Set the nonlinear solver method
   nl_pars.set("Nonlinear Solver", "Line Search Based");

   // Set the printing parameters in the "Printing" sublist
   Teuchos::ParameterList &print_pars = nl_pars.sublist("Printing");
   print_pars.set("Output Information", output_flags);

   // Sublist for line search
   Teuchos::ParameterList &search_pars = nl_pars.sublist("Line Search");
   search_pars.set("Method", "Full Step");

   // Sublist for direction
   Teuchos::ParameterList &dir_pars = nl_pars.sublist("Direction");
   dir_pars.set("Method", nl_dir);
   Teuchos::ParameterList &newton_pars = dir_pars.sublist(nl_dir);
   if (strcmp(nl_dir, "Newton") == 0) {
     // TODO: parametrize me
     newton_pars.set("Forcing Term Method", "Constant");
   }
   else if (strcmp(nl_dir, "Modified-Newton") == 0) {
     // TODO: parametrize me
     newton_pars.set("Max Age of Jacobian", 2);
   }

   // Sublist for linear solver for the Newton method
   Teuchos::ParameterList &ls_pars = newton_pars.sublist("Linear Solver");
   ls_pars.set("Aztec Solver", ls_type);
   ls_pars.set("Max Iterations", ls_max_iters);
   ls_pars.set("Tolerance", ls_tolerance);
   ls_pars.set("Size of Krylov Subspace", ls_sizeof_krylov_subspace);
   // precond stuff
   Teuchos::RCP<Precond> precond = interface->get_precond();
   if (precond_yes == false) {
     ls_pars.set("Preconditioner", "None");
   }
   else 
     if (interface->fep->is_matrix_free()) ls_pars.set("Preconditioner", "User Defined");
     else {
       if (strcasecmp(precond_type, "ML") == 0) {
         ls_pars.set("Preconditioner", "ML");
       }
       else if (strcasecmp(precond_type, "Ifpack") == 0)
         ls_pars.set("Preconditioner", "Ifpack");
       else {
         warn("Unsupported type of preconditioner.");
         ls_pars.set("Preconditioner", "None");
       }
     }
     ls_pars.set("Max Age Of Prec", 5);

     Teuchos::RCP<NOX::Epetra::Interface::Required> i_req = interface;
     Teuchos::RCP<NOX::Epetra::Interface::Jacobian> i_jac;
     Teuchos::RCP<NOX::Epetra::Interface::Preconditioner> i_prec = interface;
     Teuchos::RCP<Epetra_RowMatrix> jac_mat;
     Teuchos::RCP<NOX::Epetra::LinearSystemAztecOO> lin_sys;

     if (interface->fep->is_matrix_free()) {
       // Matrix-Free (Epetra_Operator)
       if (precond == Teuchos::null) {
	 Teuchos::RCP<NOX::Epetra::MatrixFree> mf = 
           Teuchos::rcp(new NOX::Epetra::MatrixFree(print_pars, interface, nox_sln_vec));
	 i_jac = mf;
	 lin_sys = Teuchos::rcp(new NOX::Epetra::LinearSystemAztecOO(print_pars, ls_pars, i_req,
				i_jac, mf, nox_sln_vec));
       }
       else {
	 const Teuchos::RCP<Epetra_Operator> pc = precond;
	 lin_sys = Teuchos::rcp(new NOX::Epetra::LinearSystemAztecOO(print_pars, ls_pars, i_req,
				i_prec, pc, nox_sln_vec));
       }
     }
     else {  // not Matrix Free
       // Create the Epetra_RowMatrix.
       jac_mat = Teuchos::rcp(interface->get_jacobian()->mat);
       i_jac = interface;
       lin_sys = Teuchos::rcp(new NOX::Epetra::LinearSystemAztecOO(print_pars, ls_pars, i_req,
			      i_jac, jac_mat, nox_sln_vec));
     }

     // Create the Group
     Teuchos::RCP<NOX::Epetra::Group> grp = 
              Teuchos::rcp(new NOX::Epetra::Group(print_pars, i_req, nox_sln_vec, lin_sys));

     // Create convergence tests
     Teuchos::RCP<NOX::StatusTest::Combo> converged =
	      Teuchos::rcp(new NOX::StatusTest::Combo(NOX::StatusTest::Combo::AND));

     Teuchos::RCP<NOX::StatusTest::NormF> absresid =
	      Teuchos::rcp(new NOX::StatusTest::NormF(conv.abs_resid, conv.norm_type, conv.stype));
     converged->addStatusTest(absresid);

     if (conv_flag.relresid) {
       Teuchos::RCP<NOX::StatusTest::NormF> relresid = 
         Teuchos::rcp(new NOX::StatusTest::NormF(*grp.get(), conv.rel_resid));
       converged->addStatusTest(relresid);
     }

     if (conv_flag.update) {
       Teuchos::RCP<NOX::StatusTest::NormUpdate> update =
		Teuchos::rcp(new NOX::StatusTest::NormUpdate(conv.update));
       converged->addStatusTest(update);
     }

     if (conv_flag.wrms) {
       Teuchos::RCP<NOX::StatusTest::NormWRMS> wrms =
		Teuchos::rcp(new NOX::StatusTest::NormWRMS(conv.wrms_rtol, conv.wrms_atol));
       converged->addStatusTest(wrms);
     }
     Teuchos::RCP<NOX::StatusTest::MaxIters> maxiters = 
       Teuchos::rcp(new NOX::StatusTest::MaxIters(conv.max_iters));

     Teuchos::RCP<NOX::StatusTest::FiniteValue> fv = 
       Teuchos::rcp(new NOX::StatusTest::FiniteValue);

     Teuchos::RCP<NOX::StatusTest::Combo> cmb = 
       Teuchos::rcp(new NOX::StatusTest::Combo(NOX::StatusTest::Combo::OR));

     cmb->addStatusTest(fv);
     cmb->addStatusTest(converged);
     cmb->addStatusTest(maxiters);

     Teuchos::RCP<Teuchos::ParameterList> final_pars = nl_pars_ptr;

     // Create the method
     Teuchos::RCP<NOX::Solver::Generic> solver = NOX::Solver::buildSolver(grp, cmb, final_pars);
     NOX::StatusTest::StatusType status = solver->solve();

     if (!interface->fep->is_matrix_free()) {
       jac_mat.release();	// release the ownership (we take care of jac_mat by ourselves)
     }
     bool success;
     if (status == NOX::StatusTest::Converged) {
       num_iters = solver->getNumIterations();
       residual = solver->getSolutionGroup().getNormF();
       num_lin_iters = final_pars->sublist("Direction").sublist(nl_dir).sublist("Linear Solver").sublist("Output").get("Total Number of Linear Iterations", -1);
       achieved_tol = final_pars->sublist("Direction").sublist(nl_dir).sublist("Linear Solver").sublist("Output").get("Achieved Tolerance", 0.0);

       // Get the Epetra_Vector with the final solution from the solver
#ifndef H2D_COMPLEX
       const NOX::Epetra::Group &f_grp =
	     dynamic_cast<const NOX::Epetra::Group &>(solver->getSolutionGroup());
       const Epetra_Vector &f_sln =
	     (dynamic_cast<const NOX::Epetra::Vector &>(f_grp.getX())).getEpetraVector();
#endif
       // extract solution
       int n = interface->fep->get_num_dofs();
       delete [] sln;
       sln = new scalar[n];
       memset(sln, 0, n * sizeof(double));
#ifndef H2D_COMPLEX
       f_sln.ExtractCopy(sln);
#else
#endif
       success = true;
     }
     else { // not converged
       num_iters = -1;
       success = false;
     }

     // debug
     //int n = interface->fep->get_num_dofs();
     //printf("n = %d\nvec = ", n);
     //for (int i=0; i < n; i++) printf("%g ", sln[i]);
     //printf("\n");

     return success;

#else
     return false;
#endif
}

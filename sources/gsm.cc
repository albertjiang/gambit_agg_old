//#
//# FILE: gsm.cc  implementation of GSM (Stack machine)
//#
//# $Id$
//#

//-----------------------------------------------------------------------
//                       Template instantiations
//-----------------------------------------------------------------------


class Portion;
class CallFuncObj;
class RefHashTable;
template <class T> class gGrowableStack;
#ifdef __GNUG__
#define TEMPLATE template
#elif defined __BORLANDC__
#define TEMPLATE
#pragma option -Jgd
#endif   // __GNUG__, __BORLANDC__

#include "gstack.imp"

TEMPLATE class gStack< Portion* >;
TEMPLATE class gStack< gGrowableStack< Portion* >* >;
TEMPLATE class gStack< CallFuncObj* >;
TEMPLATE class gStack< RefHashTable* >;

#include "ggrstack.imp"

TEMPLATE class gGrowableStack< Portion* >;
TEMPLATE class gGrowableStack< gGrowableStack< Portion* >* >;
TEMPLATE class gGrowableStack< CallFuncObj* >;
TEMPLATE class gGrowableStack< RefHashTable* >;

#ifdef __BORLANDC__
#pragma option -Jgx
#endif

#include "gsm.h"

#include <assert.h>

#include "glist.h"
#include "ggrstack.h"

#include "portion.h"
#include "gsmfunc.h"
#include "gsminstr.h"
#include "gsmhash.h"

#include "gblock.h"



//--------------------------------------------------------------------
//              implementation of GSM (Stack machine)
//--------------------------------------------------------------------

int GSM::_NumObj = 0;

GSM::GSM( int size, gInput& s_in, gOutput& s_out, gOutput& s_err )
:_StdIn( s_in ), _StdOut( s_out ), _StdErr( s_err )
{
#ifndef NDEBUG
  if( size <= 0 )
  {
    gerr << "  Illegal stack size specified during initialization\n";
  }
  assert( size > 0 );
#endif // NDEBUG
  
  // global function default variables initialization
  // these should be done before InitFunctions() is called
  if( _NumObj == 0 )
  {
    _INPUT  = new InputRefPortion( _StdIn );
    _OUTPUT = new OutputRefPortion( _StdOut );
    _NULL   = new OutputRefPortion( gnull );
  }

  _StackStack    = new gGrowableStack< gGrowableStack< Portion* >* >( 1 );
  _StackStack->Push( new gGrowableStack< Portion* >( size ) );
  _CallFuncStack = new gGrowableStack< CallFuncObj* >( 1 );
  _RefTableStack = new gGrowableStack< RefHashTable* >( 1 );
  _RefTableStack->Push( new RefHashTable );

  _FuncTable     = new FunctionHashTable;
  InitFunctions();  // This function is located in gsmfunc.cc

  _NumObj++;
}


GSM::~GSM()
{
  _NumObj--;

  assert( _CallFuncStack->Depth() == 0 );
  delete _CallFuncStack;

  Flush();
  delete _FuncTable;

  assert( _RefTableStack->Depth() == 1 );
  delete _RefTableStack->Pop();
  delete _RefTableStack;

  assert( _StackStack->Depth() == 1 );
  delete _StackStack->Pop();
  delete _StackStack;

  if( _NumObj == 0 )
  {
    delete _INPUT;
    delete _OUTPUT;
    delete _NULL;
  }
}


int GSM::Depth( void ) const
{
  return _Depth();
}


int GSM::MaxDepth( void ) const
{
  return _StackStack->Peek()->MaxDepth();
}



//------------------------------------------------------------------------
//                           Push() functions
//------------------------------------------------------------------------

bool GSM::Push( const bool& data )
{
  _Push( new BoolValPortion( data ) );
  return true;
}


bool GSM::Push( const long& data )
{
  _Push( new IntValPortion( data ) );
  return true;
}


bool GSM::Push( const double& data )
{
  _Push( new FloatValPortion( data ) );
  return true;
}


bool GSM::Push( const gRational& data )
{
  _Push( new RationalValPortion( data ) );
  return true;
}


bool GSM::Push( const gString& data )
{
  _Push( new TextValPortion( data ) );
  return true;
}


bool GSM::PushList( const int num_of_elements )
{ 
  int            i;
  Portion*       p;
  ListPortion*  list;
  int            insert_result;
  bool           result = true;

#ifndef NDEBUG
  if( num_of_elements < 0 )
  {
    gerr << "  Illegal number of elements requested to PushList()\n";
  }
  assert( num_of_elements >= 0 );

  if( num_of_elements > _Depth() )
  {
    gerr << "  Not enough elements in GSM to PushList()\n";
  }
  assert( num_of_elements <= Depth() );
#endif // NDEBUG

  list = new ListValPortion;
  for( i = 1; i <= num_of_elements; i++ )
  {
    p = _Pop();
    p = _ResolveRef( p );

    if( p->Type() != porREFERENCE )
    {
      insert_result = list->Insert( p->ValCopy(), 1 );
      delete p;
      if( insert_result == 0 )
      {
	_ErrorMessage( _StdErr, 35 );
	result = false;
      }
    }
    else
    {
      delete p;
      _ErrorMessage( _StdErr, 49, 0, 0, ((ReferencePortion*) p)->Value() );
      result = false;
    }
  }
  _Push( list );

  return result;
}



//--------------------------------------------------------------------
//        Stack access related functions
//--------------------------------------------------------------------



bool GSM::_VarIsDefined( const gString& var_name ) const
{
  bool result;

  assert( var_name != "" );

  if( var_name == "OUTPUT" || var_name == "INPUT" || var_name == "NULL" )
    result = true;
  else
    result = _RefTableStack->Peek()->IsDefined( var_name );
  return result;
}


bool GSM::_VarDefine( const gString& var_name, Portion* p )
{
  Portion* old_value;
  bool type_match = true;
  bool read_only = false;
  bool result = true;

  assert( var_name != "" );

  if( var_name == "OUTPUT" || var_name == "INPUT" || var_name == "NULL" )
  {
    read_only = true;
  }
  else if( _RefTableStack->Peek()->IsDefined( var_name ) )
  {
    old_value = (*_RefTableStack->Peek())( var_name );
    if( old_value->Type() != p->Type() )
    {
      if( !PortionTypeMatch( old_value->Type(), p->Type() ) )
	type_match = false;
    }
    else if( p->Type() == porLIST )
    {
      assert( old_value->Type() == porLIST );
      if( ( (ListPortion*) old_value )->DataType() != 
	 ( (ListPortion*) p )->DataType() )
      {
	if( ( (ListPortion*) p )->DataType() == porUNDEFINED )
	{
	  ( (ListPortion*) p )->
	    SetDataType( ( (ListPortion*) old_value )->DataType() );
	}
	else
	{
	  type_match = false;
	}
      }
    }
  }

  if( read_only )
  {
    _ErrorMessage( _StdErr, 46, 0, 0, var_name );
    delete p;
    result = false;
  }
  else if( !type_match )
  {
    _ErrorMessage( _StdErr, 42, 0, 0, var_name );
    delete p;
    result = false;
  }
  else
  {
    _RefTableStack->Peek()->Define( var_name, p );
  }
  return result;
}


Portion* GSM::_VarValue( const gString& var_name ) const
{
  Portion* result;

  assert( var_name != "" );

  if( var_name == "INPUT" )
    result = _INPUT;
  else if( var_name == "OUTPUT" )
    result = _OUTPUT;
  else if( var_name == "NULL" )
    result = _NULL;
  else
  {
    result = (*_RefTableStack->Peek())( var_name );
  }

  return result;
}


Portion* GSM::_VarRemove( const gString& var_name )
{
  Portion* result;

  assert( var_name != "" );

  if( var_name == "INPUT" || var_name == "OUTPUT" || var_name == "NULL" )
  {
    _ErrorMessage( _StdErr, 55, 0, 0, var_name );
    result = _VarValue( var_name )->ValCopy();
  }
  else
  {
    result = _RefTableStack->Peek()->Remove( var_name );
  }
  return result;
}



int GSM::_Depth( void ) const
{
  return _StackStack->Peek()->Depth();
}

void GSM::_Push( Portion* p )
{
  _StackStack->Peek()->Push( p );
}

Portion* GSM::_Pop( void )
{
  return _StackStack->Peek()->Pop();
}

//---------------------------------------------------------------------
//     Reference related functions
//---------------------------------------------------------------------


bool GSM::PushRef( const gString& ref )
{
  assert( ref != "" );
  _Push( new ReferencePortion( ref ) );
  return true;
}



bool GSM::Assign( void )
{
  return _BinaryOperation( "Assign" );
}



bool GSM::UnAssign( void )
{
  Portion* p;

#ifndef NDEBUG
  if( _Depth() < 1 )
  {
    gerr << "  Not enough operands to execute UnAssign()\n";
  }
  assert( _Depth() >= 1 );
#endif // NDEBUG

  p = _Pop();
  if( p->Type() == porREFERENCE )
  {
    if( _VarIsDefined( ( (ReferencePortion*) p )->Value() ) )
    {
      _Push( _VarRemove( ( (ReferencePortion*) p )->Value() ) );
      delete p;
      return true;
    }
    else
    {
      _Push( p );
      _ErrorMessage( _StdErr, 54, 0, 0, ((ReferencePortion*) p)->Value() );
      return false;
    }
  }
  else
  {
    _Push( p );
    _ErrorMessage( _StdErr, 53 );
    return false;
  }
}


//-----------------------------------------------------------------------
//                        _ResolveRef functions
//-----------------------------------------------------------------------

Portion* GSM::_ResolveRef( Portion* p )
{
  Portion*  result = 0;
  gString ref;
  
  if( p->Type() == porREFERENCE )
  {
    ref = ( (ReferencePortion*) p )->Value();

    if( !_VarIsDefined( ref ) )
    {
      result = p;
    }
    else
    {
      if( _VarValue( ref )->IsValid() )
      {
	result = _VarValue( ref )->RefCopy();
	delete p;
      }
      else
      {
	delete _VarRemove( ref );
	result = p;
      }
    }
  }
  else
  {
    result = p;
  }
  return result;
}



//------------------------------------------------------------------------
//                       binary operations
//------------------------------------------------------------------------


// Main dispatcher of built-in binary operations

bool GSM::_BinaryOperation( const gString& funcname )
{
  Portion*   p2;
  Portion*   p1;
  gList< Instruction* > prog;
  int result;

#ifndef NDEBUG
  if( _Depth() < 2 )
  {
    gerr << "  Not enough operands to perform binary operation\n";
  }
  assert( _Depth() >= 2 );
#endif // NDEBUG

  // bind the parameters in correct order
  p2 = _Pop();
  p1 = _Pop();
  _Push( p2 );
  _Push( p1 );
  
  prog.Append( new /* class */ ::InitCallFunction( funcname ) );
  prog.Append( new /* class */ ::Bind );
  prog.Append( new /* class */ ::Bind );
  prog.Append( new /* class */ ::CallFunction );
  result = Execute( prog );

  if( result == rcSUCCESS )
    return true;
  else
    return false;
}




//-----------------------------------------------------------------------
//                        unary operations
//-----------------------------------------------------------------------

bool GSM::_UnaryOperation( const gString& funcname )
{
  gList< Instruction* > prog;
  int result;

#ifndef NDEBUG
  if( _Depth() < 1 )
  {
    gerr << "  Not enough operands to perform unary operation\n";
  }
  assert( _Depth() >= 1 );
#endif // NDEBUG

  prog.Append( new /*class*/ :: InitCallFunction( funcname ) );
  prog.Append( new /*class*/ :: Bind );
  prog.Append( new /*class*/ :: CallFunction );
  result = Execute( prog );

  if( result == rcSUCCESS )
    return true;
  else
    return false;
}




//-----------------------------------------------------------------
//                      built-in operations
//-----------------------------------------------------------------

bool GSM::Add ( void )
{ return _BinaryOperation( "Plus" ); }

bool GSM::Subtract ( void )
{ return _BinaryOperation( "Minus" ); }

bool GSM::Multiply ( void )
{ return _BinaryOperation( "Times" ); }

bool GSM::Dot ( void )
{ return _BinaryOperation( "Dot" ); }

bool GSM::Divide ( void )
{ 
  return _BinaryOperation( "Divide" ); 
}

bool GSM::Negate( void )
{ return _UnaryOperation( "Negate" ); }

bool GSM::Power( void )
{ return _BinaryOperation( "Power" ); }

bool GSM::Concat ( void )
{ return _BinaryOperation( "Concat" ); }


bool GSM::IntegerDivide ( void )
{ return _BinaryOperation( "IntegerDivide" ); }

bool GSM::Modulus ( void )
{ return _BinaryOperation( "Modulus" ); }


bool GSM::EqualTo ( void )
{ return _BinaryOperation( "Equal" ); }

bool GSM::NotEqualTo ( void )
{ return _BinaryOperation( "NotEqual" ); }

bool GSM::GreaterThan ( void )
{ return _BinaryOperation( "Greater" ); }

bool GSM::LessThan ( void )
{ return _BinaryOperation( "Less" ); }

bool GSM::GreaterThanOrEqualTo ( void )
{ return _BinaryOperation( "GreaterEqual" ); }

bool GSM::LessThanOrEqualTo ( void )
{ return _BinaryOperation( "LessEqual" ); }


bool GSM::AND ( void )
{ return _BinaryOperation( "And" ); }

bool GSM::OR ( void )
{ return _BinaryOperation( "Or" ); }

bool GSM::NOT ( void )
{ return _UnaryOperation( "Not" ); }


bool GSM::Read ( void )
{ return _BinaryOperation( "Read" ); }

bool GSM::Write ( void )
{ return _BinaryOperation( "Write" ); }


bool GSM::Subscript ( void )
{
  Portion* p2;
  Portion* p1;

  assert( _Depth() >= 2 );
  p2 = _Pop();
  p1 = _Pop();

  p2 = _ResolveRef( p2 );
  p1 = _ResolveRef( p1 );

  _Push( p1 );
  _Push( p2 );

  if( p1->Type() == porTEXT )
    return _BinaryOperation( "NthChar" );
  else
    return _BinaryOperation( "NthElement" );
}


bool GSM::Child ( void )
{
  return _BinaryOperation( "NthChild" );
}



//-------------------------------------------------------------------
//               CallFunction() related functions
//-------------------------------------------------------------------

bool GSM::AddFunction( FuncDescObj* func )
{
  FuncDescObj *old_func;
  bool result;
  if( !_FuncTable->IsDefined( func->FuncName() ) )
  {
    _FuncTable->Define( func->FuncName(), func );
    return true;
  }
  else
  {
    old_func = (*_FuncTable)( func->FuncName() );
    result = old_func->Combine( func );
    if( !result )
      _ErrorMessage( _StdErr, 60, 0, 0, old_func->FuncName() );
    return result;
  }
}


#ifndef NDEBUG
void GSM::_BindCheck( void ) const
{
  if( _CallFuncStack->Depth() <= 0 )
  {
    gerr << "  The CallFunction() subsystem was not initialized by\n";
    gerr << "  calling InitCallFunction() first\n";
  }
  assert( _CallFuncStack->Depth() > 0 );

  if( _Depth() <= 0 )
  {
    gerr << "  No value found to assign to a function parameter\n";
  }
  assert( _Depth() > 0 );
}
#endif // NDEBUG


bool GSM::_Bind( const gString& param_name ) const
{
  return _CallFuncStack->Peek()->SetCurrParamIndex( param_name );
}


bool GSM::InitCallFunction( const gString& funcname )
{
  if( _FuncTable->IsDefined( funcname ) )
  {
    _CallFuncStack->Push
      ( new CallFuncObj( (*_FuncTable)( funcname ), _StdOut, _StdErr ) );
    return true;
  }
  else // ( !_FuncTable->IsDefined( funcname ) )
  {
    _ErrorMessage( _StdErr, 25, 0, 0, funcname );
    return false;
  }
}


bool GSM::Bind( const gString& param_name )
{
  return BindRef( param_name, AUTO_VAL_OR_REF );
}


bool GSM::BindVal( const gString& param_name )
{
  Portion*     param;
  bool         result = true;

#ifndef NDEBUG
  _BindCheck();
#endif // NDEBUG

  if( param_name != "" )
    result = _Bind( param_name );

  if( result )
  {
    param = _ResolveRef( _Pop() );

    if( param->IsValid() )
    {
      if( param->Type() != porREFERENCE )
	result = _CallFuncStack->Peek()->SetCurrParam( param->ValCopy() );
      else
	result = _CallFuncStack->Peek()->SetCurrParam( 0 );
    }
    else
    {
      _CallFuncStack->Peek()->SetErrorOccurred();
      _ErrorMessage( _StdErr, 61 );
      result = false;
    }
    delete param;
  }

  return result;
}


bool GSM::BindRef( const gString& param_name, bool auto_val_or_ref )
{
  Portion*           param;
  bool               result    = true;

#ifndef NDEBUG
  _BindCheck();
#endif // NDEBUG

  if( param_name != "" )
    result = _Bind( param_name );

  if( result )
  {
    param = _ResolveRef( _Pop() );

    if( param->IsValid() )
    {
      result = _CallFuncStack->Peek()->SetCurrParam( param, auto_val_or_ref );
    }
    else
    {
      _CallFuncStack->Peek()->SetErrorOccurred();
      _ErrorMessage( _StdErr, 59 );
      delete param;
      result = false;
    }
  }

  return result;
}



bool GSM::CallFunction( void )
{
  CallFuncObj*        func;
  Portion**           param;
  int                 index;
  ReferencePortion*   refp;
  Portion*            return_value;
  bool                define_result;
  bool                result = true;

#ifndef NDEBUG
  if( _CallFuncStack->Depth() <= 0 )
  {
    gerr << "  The CallFunction() subsystem was not initialized by\n";
    gerr << "  calling InitCallFunction() first\n";
  }
  assert( _CallFuncStack->Depth() > 0 );
#endif // NDEBUG

  func = _CallFuncStack->Pop();

  param = new Portion*[ func->NumParams() ];

  return_value = func->CallFunction( this, param );

  assert( return_value != 0 );

  if( return_value->Type() == porERROR )
    result = false;

  _Push( return_value );
  

  for( index = 0; index < func->NumParams(); index++ )
  {
    refp = func->GetParamRef( index );

    assert( (refp == 0) == (param[index] == 0) );

    if( refp != 0 )
    {
      define_result = _VarDefine( refp->Value(), param[ index ] );
      if( !define_result )
	result = false;
      delete refp;
    }
  }


  delete func;

  delete[] param;
  
  return result;
}


//----------------------------------------------------------------------------
//                       Execute function
//----------------------------------------------------------------------------

int GSM::Execute( gList< Instruction* >& program, bool user_func )
{
  int             result          = rcSUCCESS;
  bool            instr_success;
  bool            done            = false;
  Portion*        p;
  Instruction*    instruction;
  int             program_counter = 1;
  int             program_length  = program.Length();
  int             initial_num_of_funcs = _CallFuncStack->Depth();
  int             i;

  while( ( program_counter <= program_length ) && ( !done ) )
  {
    instruction = program[ program_counter ];
    switch( instruction->Type() )
    {
    case iQUIT:
      instr_success = true;
      result = rcQUIT;
      done = true;
      break;

    case iIF_GOTO:
      p = _Pop();
      if( p->Type() == porBOOL )
      {
	if( ( (BoolPortion*) p )->Value() )
	{
	  program_counter = ( (IfGoto*) instruction )->WhereTo();
	  assert( program_counter >= 1 && program_counter <= program_length );
	}
	else
	{
	  program_counter++;
	}
	delete p;
	instr_success = true;
      }
#ifndef NDEBUG
      else
      {
	gerr << "Instruction IfGoto called on a non-boolean data type\n";
	assert( p->Type() == porBOOL );	
	_Push( p );
	program_counter++;
	instr_success = false;
      }
#endif // NDEBUG
      break;

    case iGOTO:
      program_counter = ( (Goto*) instruction )->WhereTo();
      assert( program_counter >= 1 && program_counter <= program_length );
      instr_success = true;
      break;

    default:
      instr_success = instruction->Execute( *this );
      program_counter++;
    }

    if( !instr_success )
    {
      result = instruction->LineNumber();
      done = true;
      break;
    }
  }


  for( i = _CallFuncStack->Depth(); i > initial_num_of_funcs; i-- )
  {
    delete _CallFuncStack->Pop();
  }
  assert( _CallFuncStack->Depth() == initial_num_of_funcs );


  if( !user_func )
  {
    while( program.Length() > 0 )
    {
      delete program.Remove( 1 );
    }
  }

  return result;
}





Portion* GSM::ExecuteUserFunc( gList< Instruction* >& program, 
			      const FuncInfoType& func_info,
			      Portion** param )
{
  int rc_result;
  Portion* result;
  Portion* result_copy;
  int i;

  _RefTableStack->Push( new RefHashTable );
  _StackStack->Push( new gGrowableStack< Portion* > );


  for( i = 0; i < func_info.NumParams; i++ )
  {
    if( param[ i ] != 0 && param[ i ]->Type() != porREFERENCE )
    {
      _VarDefine( func_info.ParamInfo[ i ].Name, param[ i ] );
      param[ i ] = param[ i ]->RefCopy();
    }
  }


  rc_result = Execute( program, true );


  switch( rc_result )
  {
  case rcSUCCESS:
    switch( _Depth() )
    {
    case 0:
      result = 
	new ErrorPortion( (gString)
			 "Error: No return value" );
      break;

    default:
      result = _Pop();
      result = _ResolveRef( result );
      result_copy = result->ValCopy();
      delete result;
      result = result_copy;
      result_copy = 0;
      if( result->Type() == porERROR )
      {
	delete result;
	result = 0;
      }
      break;
    }
    break;
  case rcQUIT:
    result = 
      new ErrorPortion( (gString)
		       "Error: Interruption by user" );
    break;

  default:
    if( rc_result >= 0 )
      result = new ErrorPortion( (gString)
				"Error at line " +
				ToString( rc_result / 65536) + 
				" in function, line " +
				ToString( rc_result % 65536) +
				" in source code" );
    else
      result = 0;
    break;
  }


  for( i = 0; i < func_info.NumParams; i++ )
  {
    if( func_info.ParamInfo[ i ].PassByReference )
    {
      if( _VarIsDefined( func_info.ParamInfo[ i ].Name ) )
      {
	assert( _VarValue( func_info.ParamInfo[ i ].Name ) != 0 );
	delete param[ i ];
	param[ i ] = _VarRemove( func_info.ParamInfo[ i ].Name );
      }
    }
  }


  Flush();
  delete _StackStack->Pop();
  delete _RefTableStack->Pop();

  return result;
}




//----------------------------------------------------------------------------
//                   miscellaneous functions
//----------------------------------------------------------------------------


void GSM::Output( void )
{
  Portion*  p;

  assert( _Depth() >= 0 );

  if( _Depth() == 0 )
  {
    // _StdOut << "\n";
  }
  else
  {
    p = _Pop();
    p = _ResolveRef( p );

    if( p->IsValid() )
    {
      p->Output( _StdOut );
      if( p->Type() == porREFERENCE )
	_StdOut << " (undefined)";
      _StdOut << "\n";
    }
    else
    {
      _StdOut << "(undefined)\n";
    }
    
    _Push( p );
  }
}


void GSM::Dump( void )
{
  int  i;

  assert( _Depth() >= 0 );

  if( _Depth() == 0 )
  {
    _StdOut << "Stack : NULL\n";
  }
  else
  {
    for( i = _Depth() - 1; i >= 0; i-- )
    {
      _StdOut << "Stack element " << i << " : ";
      Output();
      Pop();
    }
  }
  _StdOut << "\n";
  
  assert( _Depth() == 0 );
}


bool GSM::Pop( void )
{
  Portion* p;
  bool result = false;

  assert( _Depth() >= 0 );

  if( _Depth() > 0 )
  {
    p = _Pop();
    delete p;
    result = true;
  }
  else
  {
    result = true;
  }
  return result;
}


void GSM::Flush( void )
{
  int       i;
  bool result;

  assert( _Depth() >= 0 );
  for( i = _Depth() - 1; i >= 0; i-- )
  {
    result = Pop();
    assert( result == true );
  }

  assert( _Depth() == 0 );
}


void GSM::Clear( void )
{
  Flush();

  assert( _RefTableStack->Depth() == 1 );
  delete _RefTableStack->Pop();
  delete _RefTableStack;

  _RefTableStack = new gGrowableStack< RefHashTable* >( 1 );
  _RefTableStack->Push( new RefHashTable );

}


void GSM::Help(void)
{
  Portion* p = _Pop();
  assert(p->Type() == porREFERENCE);
  gString funcname = ((ReferencePortion*) p)->Value();

  FuncDescObj *func;
  if( _FuncTable->IsDefined( funcname ) )
  {
    func = (*_FuncTable)( funcname );
    func->Dump(_StdOut);
  }
  else
    _ErrorMessage(_StdErr, 62, 0, 0, funcname);
}

//-----------------------------------------------------------------------
//                         _ErrorMessage
//-----------------------------------------------------------------------

void GSM::_ErrorMessage
(
 gOutput&        s,
 const int       error_num,
 const long& /*num1*/, 
 const long& /*num2*/,
 const gString&  str1,
 const gString& /*str2*/
 )
{
#if 0
  s << "GSM Error " << error_num << ":\n";
#endif // 0

  s << "GCL: ";

  switch( error_num )
  {
  case 25:
    s << "Function " << str1 << "[] undefined\n";
    break;
  case 35:
    s << "Cannot create a list of mixed types\n";
    break;
  case 42:
    s << "Cannot change the type of variable \"" << str1 << "\"\n";
    break;
  case 46:
    s << "Cannot assign to read-only variable \"" << str1 <<"\"\n";
    break;
  case 49:
    s << "Cannot insert undefined reference \"" << str1 << "\" into a list\n";
    break;
  case 53:
    s << "UnAssign[] called on a non-reference value\n";
    break;
  case 54:
    s << "UnAssign[] called on undefined reference \"" << str1 << "\"\n";
    break;
  case 55:
    s << "Cannot remove read-only variable \"" + str1 + "\"\n";
    break;
  case 59:
    s << "Cannot to pass an undefined reference to a function\n";
    break;
  case 60:
    s << "New " << str1 << "[] parameters ambiguous with existing function\n";
    break;
  case 61:
    s << "Cannot pass an undefined reference to a function\n";
    break;
  case 62:
    s << "Function " << str1 << "[] not found\n";
    break;
  default:
    s << "General error " << error_num << "\n";
  }
}

